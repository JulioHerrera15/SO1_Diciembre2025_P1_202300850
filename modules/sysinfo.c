#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sched/signal.h> // Necesario para for_each_process
#include <linux/sched/cputime.h> // Para obtener tiempo de CPU
#include <linux/mm_types.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julio Herrera");
MODULE_DESCRIPTION("Modulo Monitor de Sistema JSON");
MODULE_VERSION("1.0");

// CAMBIA ESTO POR TU CARNET REAL
#define PROC_NAME "sysinfo_so1_202300850" 

static int sysinfo_show(struct seq_file *m, void *v) {
    struct sysinfo si;
    struct task_struct *task;
    unsigned long rss, vsz;
    bool first_process = true;
    unsigned long total_ram_kb;

    // 1. Obtener Memoria Global
    si_meminfo(&si);
    // Convertir a KB
    unsigned long total_ram_mb = (si.totalram * si.mem_unit) / (1024 * 1024);
    unsigned long free_ram_mb = (si.freeram * si.mem_unit) / (1024 * 1024);
    unsigned long used_ram_mb = total_ram_mb - free_ram_mb;
    total_ram_kb = (si.totalram * si.mem_unit) / 1024;

    // 2. Iniciar JSON Global
    seq_printf(m, "{\n");
    seq_printf(m, "  \"total_ram\": %lu,\n", total_ram_mb);
    seq_printf(m, "  \"free_ram\": %lu,\n", free_ram_mb);
    seq_printf(m, "  \"used_ram\": %lu,\n", used_ram_mb);
    seq_printf(m, "  \"processes\": [\n");

    // 3. Recorrer Procesos (task_struct)
    for_each_process(task) {
        if (!first_process) {
            seq_printf(m, ",\n");
        }
        
        // Memoria del proceso (RSS y VSZ)
        long rss_kb = 0;
        long vsz_kb = 0;
        unsigned long mem_percent = 0;
        
        if (task->mm) {
            // RSS: Resident Set Size (memoria física)
            rss = get_mm_rss(task->mm);
            rss_kb = (rss * PAGE_SIZE) / 1024;
            
            // VSZ: Virtual Memory Size (memoria virtual total)
            vsz = task->mm->total_vm;
            vsz_kb = (vsz * PAGE_SIZE) / 1024;
            
            // Porcentaje de memoria
            if (total_ram_kb > 0) {
                mem_percent = (rss_kb * 100) / total_ram_kb;
            }
        }

        // Estado del proceso
        char state_char = task_state_to_char(task);
        
        // CPU Usage - calculado como porcentaje simplificado
        // Nota: El cálculo exacto de %CPU requiere muestreo en el tiempo
        // Aquí usamos una aproximación basada en utime + stime
        u64 utime = 0, stime = 0;
        unsigned long cpu_percent = 0;
        
        task_cputime_adjusted(task, &utime, &stime);
        u64 total_time = utime + stime;
        // Convertir a jiffies y calcular porcentaje aproximado
        // Nota: Este es un cálculo simplificado, idealmente necesitas delta de tiempo
        if (total_time > 0) {
            cpu_percent = (total_time * 100) / (jiffies + 1);
            if (cpu_percent > 100) cpu_percent = 0; // Limitar valores anómalos
        }
        
        // Obtener cmdline (línea de comandos)
        // Nota: Para procesos del kernel task->mm es NULL, no tienen cmdline
        char cmdline[256] = {0};
        if (task->mm) {
            // Obtener el nombre del comando de argv[0] es complejo desde kernel
            // Usaremos comm como aproximación, o puedes leer desde /proc/[pid]/cmdline
            snprintf(cmdline, sizeof(cmdline), "%s", task->comm);
        } else {
            snprintf(cmdline, sizeof(cmdline), "[%s]", task->comm);
        }
        
        // Escribir objeto JSON del proceso
        seq_printf(m, "    {\n");
        seq_printf(m, "      \"pid\": %d,\n", task->pid);
        seq_printf(m, "      \"name\": \"%s\",\n", task->comm);
        seq_printf(m, "      \"cmdline\": \"%s\",\n", cmdline);
        seq_printf(m, "      \"vsz\": %ld,\n", vsz_kb);
        seq_printf(m, "      \"rss\": %ld,\n", rss_kb);
        seq_printf(m, "      \"mem_percent\": %lu,\n", mem_percent);
        seq_printf(m, "      \"cpu_percent\": %lu,\n", cpu_percent);
        seq_printf(m, "      \"state\": \"%c\"\n", state_char);
        seq_printf(m, "    }");
        
        first_process = false;
    }

    // 4. Cerrar JSON
    seq_printf(m, "\n  ]\n");
    seq_printf(m, "}\n");

    return 0;
}

static int sysinfo_open(struct inode *inode, struct file *file) {
    return single_open(file, sysinfo_show, NULL);
}

static const struct proc_ops sysinfo_ops = {
    .proc_open = sysinfo_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init sysinfo_init(void) {
    proc_create(PROC_NAME, 0, NULL, &sysinfo_ops);
    printk(KERN_INFO "sysinfo module loaded\n");
    return 0;
}

static void __exit sysinfo_exit(void) {
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "sysinfo module unloaded\n");
}

module_init(sysinfo_init);
module_exit(sysinfo_exit);