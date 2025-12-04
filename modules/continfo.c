#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sched/signal.h>
#include <linux/pid_namespace.h> // Necesario para detectar contenedores
#include <linux/pid.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julio Herrera");
MODULE_DESCRIPTION("Modulo de Procesos de Contenedores JSON");
MODULE_VERSION("1.0");

// CAMBIA ESTO POR TU CARNET REAL
#define PROC_NAME "continfo_so1_202300850"

// Función auxiliar para calcular el porcentaje de CPU (Aproximación simple)
// Nota: Calcular %CPU instantáneo en kernel es complejo. 
// Aquí usamos el tiempo total de CPU usado / tiempo total desde que inició el proceso.
// Para mayor precisión, el Daemon de Go debería calcular deltas.
unsigned long calculate_cpu_usage(struct task_struct *task) {
    unsigned long total_time = task->utime + task->stime; // Tiempo en modo usuario + kernel
    unsigned long seconds = (jiffies - task->start_time) / HZ; // Tiempo de vida del proceso en segundos
    
    if (seconds == 0) return 0;
    
    // Multiplicamos por 100 para porcentaje, dividimos por HZ para normalizar ticks
    // (Esto es una simplificación académica)
    return (total_time * 100) / (seconds * HZ); 
}

static int continfo_show(struct seq_file *m, void *v) {
    struct task_struct *task;
    unsigned long rss;
    unsigned long vsz;
    unsigned long total_ram_pages;
    bool first_process = true;
    
    // Obtenemos RAM total para calcular porcentajes
    struct sysinfo si;
    si_meminfo(&si);
    total_ram_pages = si.totalram;

    seq_printf(m, "[\n");

    for_each_process(task) {
        // FILTRO MAGICO DE CONTENEDORES:
        // Verificamos si el proceso tiene un mapa de memoria (no es hilo del kernel)
        // Y verificamos si está en un namespace diferente al root.
        // task_active_pid_ns(task) devuelve el namespace del proceso.
        // Si el nivel (level) > 0, significa que está "encapsulado" (contenedor).
        if (task->mm && task_active_pid_ns(task)->level > 0) {
            
            if (!first_process) {
                seq_printf(m, ",\n");
            }

            // --- Recolección de datos ---
            
            // 1. Memoria Física (RSS) en KB
            rss = get_mm_rss(task->mm) * (PAGE_SIZE / 1024);
            
            // 2. Memoria Virtual (VSZ) en KB
            vsz = (task->mm->total_vm) * (PAGE_SIZE / 1024);

            // 3. Porcentaje de RAM
            // (RSS paginas / Total RAM paginas) * 100
            // Usamos enteros, asi que multiplicamos por 10000 para tener 2 decimales implicitos si se quisiera
            unsigned long rss_pages = get_mm_rss(task->mm);
            unsigned long mem_percent = (rss_pages * 100) / total_ram_pages;

            // 4. Porcentaje de CPU
            unsigned long cpu_percent = calculate_cpu_usage(task);

            // --- Escritura JSON ---
            seq_printf(m, "  {\n");
            seq_printf(m, "    \"pid\": %d,\n", task->pid);
            seq_printf(m, "    \"name\": \"%s\",\n", task->comm);
            // El ID del contenedor es difícil de sacar directo del task_struct.
            // El PDF permite "Línea de comando ... O ID". 
            // Mostraremos el PID del padre como referencia o el mismo PID.
            seq_printf(m, "    \"container_id_or_ppid\": %d,\n", task->real_parent->pid); 
            seq_printf(m, "    \"vsz_kb\": %lu,\n", vsz);
            seq_printf(m, "    \"rss_kb\": %lu,\n", rss);
            seq_printf(m, "    \"mem_usage_percent\": %lu,\n", mem_percent);
            seq_printf(m, "    \"cpu_usage_percent\": %lu,\n", cpu_percent);
            seq_printf(m, "    \"state\": \"%c\"\n", task_state_to_char(task));
            seq_printf(m, "  }");

            first_process = false;
        }
    }

    seq_printf(m, "\n]\n");
    return 0;
}

static int continfo_open(struct inode *inode, struct file *file) {
    return single_open(file, continfo_show, NULL);
}

static const struct proc_ops continfo_ops = {
    .proc_open = continfo_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init continfo_init(void) {
    // Crea el archivo /proc/continfo_so1_CARNET
    proc_create(PROC_NAME, 0, NULL, &continfo_ops);
    printk(KERN_INFO "Container info module loaded\n");
    return 0;
}

static void __exit continfo_exit(void) {
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "Container info module unloaded\n");
}

module_init(continfo_init);
module_exit(continfo_exit);