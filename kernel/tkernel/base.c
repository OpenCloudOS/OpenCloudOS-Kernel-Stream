#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/tkernel.h>
#include <linux/init.h>

struct proc_dir_entry *proc_tkernel;
EXPORT_SYMBOL(proc_tkernel);

static struct ctl_table placeholder_vars[] = {
	{},
};

static int __init tkernel_init(void)
{
	proc_tkernel = proc_mkdir("tkernel", NULL);
	register_sysctl_init("tkernel", placeholder_vars);
	return 0;
}
early_initcall(tkernel_init);
