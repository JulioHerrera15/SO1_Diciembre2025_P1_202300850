package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings" 
	"syscall"
	"time"

	_ "github.com/mattn/go-sqlite3"
)

// --- CONFIGURACIÓN ---
const CARNET = "202300850"
const DB_NAME = "logs_so1.db"
// Ruta absoluta a script bash
const SCRIPT_PATH = "/home/vm/proyecto1/container.sh"

var PROC_CONTINFO = fmt.Sprintf("/proc/continfo_so1_%s", CARNET)
var PROC_SYSINFO = fmt.Sprintf("/proc/sysinfo_so1_%s", CARNET)

// --- ESTRUCTURAS ---
type Process struct {
	Pid         int     `json:"pid"`
	Name        string  `json:"name"`
	ContainerID int     `json:"container_id_or_ppid"`
	RSS         int     `json:"rss_kb"`
	CpuPercent  float64 `json:"cpu_usage_percent"`
}

type SystemInfo struct {
	TotalRam int64 `json:"total_ram"`
	FreeRam  int64 `json:"free_ram"`
	UsedRam  int64 `json:"used_ram"`
}

func main() {
	limpiarPantalla()
	fmt.Println("========================================")
	fmt.Printf("   DAEMON Go - SO1 %s\n", CARNET)
	fmt.Println("========================================")

	cwd, _ := os.Getwd()
	dbPath := filepath.Join(cwd, DB_NAME)

	initDB(dbPath)
	installKernelModules()
	initGrafana(cwd, dbPath)
	setupCronjob()
	defer removeCronjob()

	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-c
		fmt.Println("\n[!] Deteniendo servicios...")
		removeCronjob()
		fmt.Println("[*] Deteniendo contenedor de Grafana...")
		err := exec.Command("docker", "stop", "grafana_so1").Run()
		if err != nil {
			fmt.Printf("   [WARNING] Error deteniendo Grafana: %v\n", err)
		} else {
			fmt.Println("   [OK] Grafana detenido correctamente.")
		}
		fmt.Println("[*] ¡Adiós!.")
		os.Exit(0)
	}()

	ticker := time.NewTicker(20 * time.Second)
	defer ticker.Stop()

	loopPrincipal(dbPath)

	for {
		select {
		case <-ticker.C:
			loopPrincipal(dbPath)
		}
	}
}

func loopPrincipal(dbPath string) {
	loc, _ := time.LoadLocation("America/Guatemala")
	now := time.Now().In(loc)
	fmt.Printf("\n--- Ciclo: %s ---\n", now.Format("2006-01-02 15:04:05"))

	// A. Leer Sysinfo
	dataSys, err := ioutil.ReadFile(PROC_SYSINFO)
	if err == nil {
		var sysInfo SystemInfo
		if errJson := json.Unmarshal(dataSys, &sysInfo); errJson == nil {
			guardarSystemStats(dbPath, sysInfo)
			fmt.Printf("Sistema: RAM Total %d MB | Libre %d MB\n", sysInfo.TotalRam, sysInfo.FreeRam)
		}
	}

	// B. Leer Contenedores
	dataCont, err := ioutil.ReadFile(PROC_CONTINFO)
	if err != nil {
		fmt.Printf("[ERROR] Error leyendo Contenedores: %v\n", err)
		return
	}

	var procesos []Process
	json.Unmarshal(dataCont, &procesos)

	var highPids []Process
	var lowPids []Process

	for _, p := range procesos {
		// INMUNIDAD
		if p.Name == "grafana-server" || p.Name == "grafana" || p.Name == "gpx_sqlite-data" {
			continue 
		}

		esAlto := false
		if p.Name == "python" || p.Name == "stress" {
			esAlto = true
		} else if p.RSS > 51200 || p.CpuPercent > 5.0 {
			esAlto = true
		}

		if esAlto {
			highPids = append(highPids, p)
		} else {
			lowPids = append(lowPids, p)
		}
		
		guardarLogProceso(dbPath, p)
	}

	gestionarEstabilidad(highPids, lowPids)
}

func gestionarEstabilidad(high []Process, low []Process) {
	if len(high) > 2 {
		sobrantes := len(high) - 2
		fmt.Printf("[WARNING]  Exceso Alto Consumo (+%d). Eliminando...\n", sobrantes)
		for i := 0; i < sobrantes; i++ {
			matarYBorrarContenedor(high[i].Pid)
		}
	} else {
		fmt.Printf("[OK] Alto Consumo: %d/2\n", len(high))
	}

	if len(low) > 3 {
		sobrantes := len(low) - 3
		fmt.Printf("[WARNING]  Exceso Bajo Consumo (+%d). Eliminando...\n", sobrantes)
		for i := 0; i < sobrantes; i++ {
			matarYBorrarContenedor(low[i].Pid)
		}
	} else {
		fmt.Printf("[OK] Bajo Consumo: %d/3\n", len(low))
	}
}

func matarYBorrarContenedor(pid int) {
	// 1. Necesitamos saber qué contenedor tiene este PID.
	// Ejecutamos "docker ps -q" para obtener todos los IDs activos
	cmd := exec.Command("docker", "ps", "-q")
	output, _ := cmd.Output()
	containerIDs := strings.Fields(string(output))

	encontrado := false

	for _, cid := range containerIDs {
		// 2. Inspeccionamos cada contenedor para ver su PID real
		inspectCmd := exec.Command("docker", "inspect", "--format", "{{.State.Pid}}", cid)
		out, _ := inspectCmd.Output()
		dockerPid := strings.TrimSpace(string(out))
		
		// 3. Si coincide con el PID que queremos matar...
		if dockerPid == fmt.Sprintf("%d", pid) {
			fmt.Printf("   [DELETE]  Eliminando Contenedor ID: %s (PID %d)\n", cid, pid)
			
			// 4. Docker RM -F (Fuerza detención y borrado)
			err := exec.Command("docker", "rm", "-f", cid).Run()
			if err != nil {
				fmt.Printf("      Error al borrar: %v\n", err)
			}
			encontrado = true
			break // Ya lo matamos, salimos del bucle
		}
	}

	// Fallback: Si por alguna razón Docker no dio el PID (ej. proceso zombie),
	// usamos kill -9 como último recurso para liberar la RAM.
	if !encontrado {
		fmt.Printf("   [KILL] Contenedor no encontrado en Docker, matando proceso PID %d directamente...\n", pid)
		exec.Command("kill", "-9", fmt.Sprintf("%d", pid)).Run()
	}
}

func initGrafana(cwd string, dbPath string) {
	fmt.Println("[*] Inicializando Grafana con Docker Compose...")

	// Comando: docker compose up -d
	// Esto inicia el contenedor definido en docker-compose.yml
	cmd := exec.Command("docker", "compose", "up", "-d")
	
	// Redirigimos la salida para ver si hay errores en consola
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	err := cmd.Run()
	if err != nil {
		// Fallback: Si falla "docker compose", intentamos con el viejo "docker-compose"
		fmt.Println("   [WARNING] 'docker compose' falló, intentando 'docker-compose' (v1)...")
		cmdV1 := exec.Command("docker-compose", "up", "-d")
		if errV1 := cmdV1.Run(); errV1 != nil {
			fmt.Printf("[ERROR] Error crítico iniciando Grafana: %v\n", errV1)
			return
		}
	}

	fmt.Println("[OK] Grafana está corriendo (Persistencia activada).")
}

func installKernelModules() {
	fmt.Println("[*] Compilando módulos del kernel...")
	cmdMake := exec.Command("make", "-C", "../modules")
	cmdMake.Stdout = os.Stdout
	cmdMake.Stderr = os.Stderr
	err := cmdMake.Run()
	if err != nil {
		fmt.Printf("[ERROR] Error compilando módulos del kernel: %v\n", err)
		return
	}
	fmt.Println("[OK] Módulos del kernel compilados correctamente.")
	fmt.Println("[*] Instalando módulos del kernel necesarios...")
	modules := []string{"../modules/continfo.ko", "../modules/sysinfo.ko"}
	for _, mod := range modules {
		err := exec.Command("sudo", "insmod", mod).Run()
		if err != nil {
			if err.Error() == "exit status 1" {
				fmt.Printf("   [OK] Módulo %s ya está cargado.\n", mod)
				continue
			} else{
				fmt.Printf("   [WARNING] No se pudo cargar el módulo %s: %v\n", mod, err)
			}
		} else {
			fmt.Printf("   [OK] Módulo %s cargado correctamente.\n", mod)
		}
	}
	
}

func initDB(dbPath string) {
	if _, err := os.Stat(dbPath); os.IsNotExist(err) {
		file, err := os.Create(dbPath)
		if err != nil { log.Fatal(err) }
		file.Close()
		os.Chmod(dbPath, 0777)
	}
	db, err := sql.Open("sqlite3", dbPath)
	if err != nil { log.Fatal(err) }
	defer db.Close()

	sqlLogs := `CREATE TABLE IF NOT EXISTS logs (id INTEGER PRIMARY KEY AUTOINCREMENT, pid INTEGER, name TEXT, ram_usage INTEGER, cpu_usage REAL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);`
	sqlSys := `CREATE TABLE IF NOT EXISTS system_stats (id INTEGER PRIMARY KEY AUTOINCREMENT, total_ram INTEGER, free_ram INTEGER, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);`

	db.Exec(sqlLogs)
	db.Exec(sqlSys)
}

func guardarLogProceso(dbPath string, p Process) {
	db, _ := sql.Open("sqlite3", dbPath)
	defer db.Close()
	stmt, _ := db.Prepare("INSERT INTO logs(pid, name, ram_usage, cpu_usage) values(?,?,?,?)")
	defer stmt.Close()
	stmt.Exec(p.Pid, p.Name, p.RSS, p.CpuPercent)
}

func guardarSystemStats(dbPath string, sys SystemInfo) {
	db, _ := sql.Open("sqlite3", dbPath)
	defer db.Close()
	stmt, _ := db.Prepare("INSERT INTO system_stats(total_ram, free_ram) values(?,?)")
	defer stmt.Close()
	stmt.Exec(sys.TotalRam, sys.FreeRam)
}

func setupCronjob() {
	fmt.Println("[*] Configurando Cronjob...")
	cmd := exec.Command("crontab", "-l")
	output, _ := cmd.Output()
	currentCron := string(output)
	newEntry := fmt.Sprintf("* * * * * %s\n", SCRIPT_PATH)
	
	cmdApply := exec.Command("crontab", "-")
	stdin, _ := cmdApply.StdinPipe()
	cmdApply.Start()
	stdin.Write([]byte(currentCron + newEntry))
	stdin.Close()
	cmdApply.Wait()
}

func removeCronjob() {
	exec.Command("crontab", "-r").Run()
	fmt.Println("[*] Cronjob eliminado.")
}

func limpiarPantalla() {
	fmt.Print("\033[H\033[2J")
}