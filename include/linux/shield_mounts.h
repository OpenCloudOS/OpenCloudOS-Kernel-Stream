#ifndef __SHIELD_MOUNTS_H__
#define __SHIELD_MOUNTS_H__

#ifdef CONFIG_TKERNEL_SHIELD_MOUNTS
bool is_mount_shielded(struct task_struct *task,
		const char *dev_name, struct vfsmount *mnt);
#else
static inline bool is_mount_shielded(struct task_struct *task,
		const char *dev_name, struct vfsmount *mnt)
{
	return false;
}
#endif

#endif /* __SHIELD_MOUNTS_H__ */
