#include <linux/sysctl.h>
#include <linux/proc_fs.h>
extern struct proc_dir_entry *proc_tkernel;
extern const struct ctl_path tkernel_ctl_path[];

/* prot_sock_flag */
#ifdef CONFIG_TKERNEL_NONPRIV_NETBIND
extern bool nonpriv_prot_sock_flag[];
static inline bool check_nonpriv_prot_sock(int num)
{
	return nonpriv_prot_sock_flag[num];
}
#else
static inline bool check_nonpriv_prot_sock(int num)
{
	return false;
}
#endif
