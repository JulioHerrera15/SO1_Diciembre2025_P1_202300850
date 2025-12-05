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
#include <linux/sched/cputime.h>
#include <linux/mm_types.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julio Herrera");
MODULE_DESCRIPTION("Modulo Monitor de Sistema JSON");
MODULE_VERSION("1.1"); // Version actualizada

// CAMBIA ESTO POR TU CARNET REAL
#define PROC_NAME "sysinfo_so1_202300850" 

static int sysinfo_show(struct seq_file *m, void *v) {
    struct sysinfo si;
    struct task_struct *task;
    unsigned long rss, vsz;
    bool first_process = true;
    unsigned long total_ram_pages;
    unsigned long total_jiffies;

    si_meminfo(&si);
    total_jiffies = jiffies;
    // Convertir a MB
    unsigned long total_ram_mb = (si.totalram * si.mem_unit) / (1024 * 1024);
    unsigned long free_ram_mb = (si.freeram * si.mem_unit) / (1024 * 1024);
    unsigned long used_ram_mb = total_ram_mb - free_ram_mb;
    
    total_ram_pages = si.totalram; // Usamos paginas para calculo preciso

    // 2. Iniciar JSON Global
    seq_printf(m, "{\n");
    seq_printf(m, "  \"total_ram\": %lu,\n", total_ram_mb);
    seq_printf(m, "  \"free_ram\": %lu,\n", free_ram_mb);
    seq_printf(m, "  \"used_ram\": %lu,\n", used_ram_mb);
    seq_printf(m, "  \"processes\": [\n");

    // 3. Recorrer Procesos
    for_each_process(task) {
        unsigned long cpu_percent_100 = 0;
        if (!first_process) {
            seq_printf(m, ",\n");
        }
        
        long rss_kb = 0;
        long vsz_kb = 0;
        unsigned long mem_percent_100 = 0;
        
        
        // --- MEMORIA ---
        if (task->mm) {
            rss = get_mm_rss(task->mm);
            rss_kb = (rss * PAGE_SIZE) / 1024;
            vsz = task->mm->total_vm;
            vsz_kb = (vsz * PAGE_SIZE) / 1024;
            
            // Calculo porcentaje RAM (Punto fijo)
            if (total_ram_pages > 0) {
                mem_percent_100 = (rss * 10000) / total_ram_pages;
            }
        }

        // --- CPU ---
        // Usamos tiempo de vida del proceso en lugar de jiffies del sistema
        // para que sea el promedio de vida del proceso, no diluido por uptime del sistema.

        unsigned long total_time = task->utime + task->stime;
        if (total_jiffies > 0) {
            cpu_percent_100 = (total_time * 10000) / total_jiffies;
            cpu_percent_100 = cpu_percent_100 / num_online_cpus();
        }
        
        // Limpieza de cmdline
        char cmdline[256] = {0};
        if (task->mm) {
            snprintf(cmdline, sizeof(cmdline), "%s", task->comm);
        } else {
            snprintf(cmdline, sizeof(cmdline), "[%s]", task->comm);
        }
        
        char state_char = task_state_to_char(task);

        // --- SALIDA JSON ---
        seq_printf(m, "    {\n");
        seq_printf(m, "      \"pid\": %d,\n", task->pid);
        seq_printf(m, "      \"name\": \"%s\",\n", task->comm);
        seq_printf(m, "      \"cmdline\": \"%s\",\n", cmdline); // Opcional si quieres reducir ruido
        seq_printf(m, "      \"vsz\": %ld,\n", vsz_kb);
        seq_printf(m, "      \"rss\": %ld,\n", rss_kb);
        
        // Decimales manuales
        seq_printf(m, "      \"mem_percent\": %lu.%02lu,\n", mem_percent_100 / 100, mem_percent_100 % 100);
        seq_printf(m, "      \"cpu_percent\": %lu.%02lu,\n", cpu_percent_100 / 100, cpu_percent_100 % 100);
        
        seq_printf(m, "      \"state\": \"%c\"\n", state_char);
        seq_printf(m, "    }");
        
        first_process = false;
    }

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