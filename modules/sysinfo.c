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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julio Herrera");
MODULE_DESCRIPTION("Modulo Monitor de Sistema JSON");
MODULE_VERSION("1.0");

// CAMBIA ESTO POR TU CARNET REAL
#define PROC_NAME "sysinfo_so1_202300850" 

static int sysinfo_show(struct seq_file *m, void *v) {
    struct sysinfo si;
    struct task_struct *task;
    unsigned long rss;
    bool first_process = true;

    // 1. Obtener Memoria Global
    si_meminfo(&si);
    // Convertir a MB (dependiendo de si.mem_unit, usualmente bytes o pages)
    // Una forma segura es convertir todo a Bytes y luego dividir
    unsigned long total_ram_mb = (si.totalram * si.mem_unit) / (1024 * 1024);
    unsigned long free_ram_mb = (si.freeram * si.mem_unit) / (1024 * 1024);
    unsigned long used_ram_mb = total_ram_mb - free_ram_mb;
    // Ojo: Linux usa mucha RAM para buffer/cache, "used" real suele ser complexo,
    // pero para este proyecto (Total - Free) suele ser aceptado.

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
        
        // Memoria del proceso (RSS)
        // Cuidado: Procesos del kernel (mm es NULL) no tienen RSS
        long rss_kb = 0;
        if (task->mm) {
            rss = get_mm_rss(task->mm);
            rss_kb = rss * 4; // Asumiendo paginas de 4KB, mejor usar (rss * PAGE_SIZE) / 1024
        }

        // Estado del proceso
        char state_char = task_state_to_char(task);
        
        // Escribir objeto JSON del proceso
        seq_printf(m, "    {\n");
        seq_printf(m, "      \"pid\": %d,\n", task->pid);
        seq_printf(m, "      \"name\": \"%s\",\n", task->comm);
        seq_printf(m, "      \"state\": \"%c\",\n", state_char);
        seq_printf(m, "      \"rss_kb\": %ld\n", rss_kb);
        // Nota: Calcular %CPU en kernel es muy difícil en una sola lectura,
        // muchos estudiantes lo calculan en el Daemon de Go o ponen 0 aquí.
        // El %RAM se puede calcular aquí: (rss_kb / total_ram_kb) * 100
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