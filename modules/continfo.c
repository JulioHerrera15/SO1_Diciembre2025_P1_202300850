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
#include <linux/pid_namespace.h>
#include <linux/pid.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julio Herrera");
MODULE_DESCRIPTION("Modulo de Procesos de Contenedores JSON");
MODULE_VERSION("1.1"); // Version actualizada

// CAMBIA ESTO POR TU CARNET REAL
#define PROC_NAME "continfo_so1_202300850"

static int continfo_show(struct seq_file *m, void *v) {
    struct task_struct *task;
    unsigned long rss;
    unsigned long vsz;
    unsigned long total_ram_pages;
    bool first_process = true;
    
    // Obtenemos RAM total
    struct sysinfo si;
    si_meminfo(&si);
    total_ram_pages = si.totalram;

    seq_printf(m, "[\n");

    for_each_process(task) {
        if (task->mm && task_active_pid_ns(task)->level > 0) {
            
            if (!first_process) {
                seq_printf(m, ",\n");
            }

            // 1. Memoria RSS y VSZ
            rss = get_mm_rss(task->mm) * (PAGE_SIZE / 1024);
            vsz = (task->mm->total_vm) * (PAGE_SIZE / 1024);

            // 2. Cálculo Porcentaje RAM (Con decimales)
            // Multiplicamos por 10000 para guardar 2 decimales en el entero
            // Ej: 1550 -> 15.50%
            unsigned long rss_pages = get_mm_rss(task->mm);
            unsigned long mem_percent_100 = (rss_pages * 10000) / total_ram_pages;
            
            // 3. Cálculo Porcentaje CPU (Con decimales)
            unsigned long cpu_usage_percent = 0;
            unsigned long total_time = task->utime + task->stime;
            unsigned long total_jiffies = jiffies;

            if (total_jiffies > 0) {
                cpu_usage_percent = (total_time * 10000) / total_jiffies;
                cpu_usage_percent = cpu_usage_percent / num_online_cpus();
            }

            // --- Escritura JSON ---
            // Truco: Imprimimos parte entera y parte decimal separadas por punto
            seq_printf(m, "  {\n");
            seq_printf(m, "    \"pid\": %d,\n", task->pid);
            seq_printf(m, "    \"name\": \"%s\",\n", task->comm);
            seq_printf(m, "    \"container_id_or_ppid\": %d,\n", task->real_parent->pid); 
            seq_printf(m, "    \"vsz_kb\": %lu,\n", vsz);
            seq_printf(m, "    \"rss_kb\": %lu,\n", rss);
            
            // Formato decimal manual: %lu.%02lu
            seq_printf(m, "    \"mem_usage_percent\": %lu.%02lu,\n", mem_percent_100 / 100, mem_percent_100 % 100);
            seq_printf(m, "    \"cpu_usage_percent\": %lu.%02lu,\n", cpu_usage_percent / 100, cpu_usage_percent % 100);
            
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