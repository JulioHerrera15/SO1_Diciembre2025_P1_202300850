# MANUAL TÉCNICO

## Proyecto 1: Desarrollo de un módulo de kernel en C y un daemon en Go para el monitoreo de procesos y contenedores en Linux

**Sistemas Operativos 1**

---

## 1. Estructura del Proyecto

El código fuente está organizado de la siguiente manera:

```
proyecto1/
├── modules/          # Código C de módulos del kernel (sysinfo.c, continfo.c) y Makefile
├── docker/           # Dockerfiles para imágenes de contenedores de prueba
├── go-daemon/        # Servicio principal (main.go), docker-compose.yml y SQLite
├── container.sh     # Script bash para generar contenedores automáticamente
```

---

## 2. Módulos de Kernel (C)

### Descripción

Se desarrollaron dos módulos cargables del kernel (LKM) utilizando las librerías `linux/module.h` y `linux/proc_fs.h` para exponer información en el sistema de archivos virtual `/proc`.

### 2.1 sysinfo.c

**Propósito**: Leer información global de la memoria RAM.

**Funciones Clave**:
- `sysinfo_show()`: Utiliza `si_meminfo()` para obtener estructuras internas de memoria y calcular RAM total y libre. Escribe un JSON en `/proc/sysinfo_so1_...`.
- **Cálculos**: Convierte unidades de memoria usando `si.mem_unit` para garantizar compatibilidad entre arquitecturas.

**Campos exportados**:
```json
{
  "total_ram": 8192,
  "free_ram": 2048,
  "used_ram": 6144,
  "processes": [...]
}
```

### 2.2 continfo.c

**Propósito**: Listar procesos específicos de contenedores.

**Funciones Clave**:
- `continfo_show()`: Itera sobre todos los procesos usando `for_each_process`.
- **Filtro de Contenedores**: Verifica `task->mm` y comprueba si `task_active_pid_ns(task)->level > 0`. Esto aísla procesos que no pertenecen al namespace raíz (Docker).
- **Punto Fijo (Fixed Point)**: Dado que el Kernel no soporta coma flotante, los porcentajes de CPU y RAM se calculan multiplicando por 10000 para conservar 2 decimales de precisión en el JSON resultante.

### Comandos de Gestión

```bash
# Compilación
make all

# Carga de módulo
sudo insmod sysinfo.ko
sudo insmod continfo.ko

# Descarga de módulo
sudo rmmod sysinfo
sudo rmmod continfo

# Verificación de logs
dmesg | tail
```

---

## 3. Daemon en Go

### Descripción

Es el **orquestador del sistema**. Se ejecuta con privilegios elevados para gestionar Docker, Cronjobs y lectura de archivos protegidos del kernel.

### Funciones Principales (main.go)

#### `loopPrincipal()`
- Se ejecuta cada **20 segundos** (Ticker)
- Lee los archivos `/proc/sysinfo_so1_...` y `/proc/continfo_so1_...`
- Deserializa el JSON y clasifica procesos en "Alto" o "Bajo" consumo
- Aplica la lógica de estabilización del sistema

#### `installKernelModules()`
- Compila los módulos del kernel usando `make` en la carpeta `modules`
- Instala los módulos del kernel `sysinfo` y `continfo`

#### `initGrafana()`
- Reemplaza la necesidad de ejecutar Docker Compose manualmente
- Utiliza `exec.Command` para levantar el contenedor de Grafana
- Monta volúmenes de persistencia (`./grafana_data`)
- Configura plugins necesarios

#### `setupCronjob()` / `removeCronjob()`
- Manipula directamente el **crontab** del sistema
- Inyecta la ejecución del script bash cada minuto
- Limpia el crontab al salir del programa

#### `matarYBorrarContenedor()`
1. Recibe un **PID** del Kernel
2. Busca en Docker qué **Container ID** corresponde a ese PID
3. Ejecuta `docker rm -f <ContainerID>` para asegurar limpieza total

### Dependencias Externas

```go
import (
    "github.com/mattn/go-sqlite3"  // Driver SQLite
    "os/exec"                       // Comandos del sistema
    "time"                          // Manejo de Tickers
)
```

**Requisitos**:
- Docker Engine y CLI
- Go 1.20+
- SQLite3

---

## 4. Decisiones de Diseño y Problemas Resueltos

### A. Uso de Aritmética de Punto Fijo en el Kernel

**Problema**: El Kernel de Linux entra en pánico si se usan registros de coma flotante (`float`, `double`) sin guardar el contexto.

**Solución**: Se implementó lógica de enteros. 

**Ejemplo**: Para obtener `12.5%`, se calcula `1250` y en la impresión JSON se formatea como `12.50`.

```c
unsigned long percent = (value * 10000) / total;
seq_printf(m, "\"percent\": %lu.%02lu", percent / 100, percent % 100);
```

---

### B. Inmunidad de Grafana (El problema del auto-boicot)

**Problema**: Grafana consume cerca de **100MB de RAM**. El Daemon lo detectaba como "Alto Consumo" y lo eliminaba, causando que el dashboard se cayera.

**Solución**: Se implementó una **"Lista Blanca"** en `main.go`. 

```go
// Procesos inmunes
whitelist := []string{"grafana-server", "gpx_sqlite-data", "systemd"}

if contains(whitelist, proceso.Name) {
    continue // No eliminar
}
```

---

### C. Persistencia de Datos

**Problema**: Al reiniciar el Daemon, Grafana perdía los Dashboards creados.

**Solución**: Se configuraron **volúmenes de Docker** (`-v`) apuntando a carpetas locales:
- `./grafana_data` → Configuración y plugins de Grafana
- `/tmp/logs_so1.db` → Base de datos SQLite con métricas históricas

```bash
docker run -d \
  -v ./grafana_data:/var/lib/grafana \
  -v /tmp/logs_so1.db:/var/lib/grafana/data.db \
  grafana/grafana
```

---

### D. Error OOM (Out Of Memory) 137

**Problema**: Los contenedores de prueba de memoria consumían toda la RAM de la VM, causando que Linux matara procesos aleatorios (incluyendo Grafana).

**Solución**:
1. Se activó un archivo **Swap de 2GB** en el sistema operativo:
   ```bash
   sudo fallocate -l 2G /swapfile
   sudo chmod 600 /swapfile
   sudo mkswap /swapfile
   sudo swapon /swapfile
   ```

2. Se establecieron **límites duros** en el comando `docker run` dentro del script bash:
   ```bash
   docker run --memory="100m" --cpus="0.5" img_alto_mem
   ```

---

## 5. Automatización y Cronjob

El archivo `container.sh` es ejecutado automáticamente por el **Cronjob** gestionado por Go. Este script genera aleatoriamente contenedores de 3 tipos:

| Tipo | Imagen | Consumo |
|------|--------|---------|
| **Sleep** | `img_bajo` | Bajo consumo (idle) |
| **Python Math** | `img_alto_cpu` | Alto consumo de CPU |
| **Python Bytearray** | `img_alto_mem` | Alto consumo de RAM |

**Configuración del Cron**:
```bash
* * * * * /home/vm/proyecto1/bash/container.sh >> /tmp/cron_so1.log 2>&1
```

Esto garantiza un **flujo constante de datos** para validar la lógica de estabilización del Daemon.

---