/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BLK_CGROUP_H
#define _BLK_CGROUP_H
/*
 * Common Block IO controller cgroup interface
 *
 * Based on ideas and code from CFQ, CFS and BFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2009 Vivek Goyal <vgoyal@redhat.com>
 * 	              Nauman Rafique <nauman@google.com>
 */

#include <linux/cgroup.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/u64_stats_sync.h>
#include <linux/seq_file.h>
#include <linux/radix-tree.h>
#include <linux/blkdev.h>
#include <linux/atomic.h>
#include <linux/kthread.h>
#include <linux/fs.h>

#define FC_APPID_LEN              129

#ifdef CONFIG_BLK_CGROUP

enum blkg_iostat_type {
	BLKG_IOSTAT_READ,
	BLKG_IOSTAT_WRITE,
	BLKG_IOSTAT_DISCARD,

	BLKG_IOSTAT_NR,
};

struct blkcg_dkstats {
#ifdef	CONFIG_SMP
	struct disk_stats __percpu 	*dkstats;
	struct list_head		alloc_node;
#else
	struct disk_stats		dkstats;
#endif
	struct block_device		*part;
	struct list_head		list_node;
	struct rcu_head			rcu_head;
};

struct blkcg_gq;
struct blkg_policy_data;

struct blkcg {
	struct cgroup_subsys_state	css;
	spinlock_t			lock;
	refcount_t			online_pin;

	struct radix_tree_root		blkg_tree;
	struct blkcg_gq	__rcu		*blkg_hint;
	struct hlist_head		blkg_list;

	struct blkcg_policy_data	*cpd[BLKCG_MAX_POLS];

	struct list_head		all_blkcgs_node;
#ifdef CONFIG_BLK_CGROUP_FC_APPID
	char                            fc_app_id[FC_APPID_LEN];
#endif
#ifdef CONFIG_CGROUP_WRITEBACK
	struct list_head		cgwb_list;
#endif

#ifdef CONFIG_BLK_DEV_THROTTLING
	struct percpu_counter		nr_dirtied;
	unsigned long			bw_time_stamp;
	unsigned long			dirtied_stamp;
	unsigned long			dirty_ratelimit;
	unsigned long long		buffered_write_bps;
#endif
	/* disk_stats for per blkcg */
	unsigned int			dkstats_on;
	struct list_head		dkstats_list;
	struct blkcg_dkstats		*dkstats_hint;

	KABI_RESERVE(1);
	KABI_RESERVE(2);
	KABI_RESERVE(3);
	KABI_RESERVE(4);
};

struct blkg_iostat {
	u64				bytes[BLKG_IOSTAT_NR];
	u64				ios[BLKG_IOSTAT_NR];
};

struct blkg_iostat_set {
	struct u64_stats_sync		sync;
	struct blkg_iostat		cur;
	struct blkg_iostat		last;
};

/* association between a blk cgroup and a request queue */
struct blkcg_gq {
	/* Pointer to the associated request_queue */
	struct request_queue		*q;
	struct list_head		q_node;
	struct hlist_node		blkcg_node;
	struct blkcg			*blkcg;

	/* all non-root blkcg_gq's are guaranteed to have access to parent */
	struct blkcg_gq			*parent;

	/* reference count */
	struct percpu_ref		refcnt;

	/* is this blkg online? protected by both blkcg and q locks */
	bool				online;

	struct blkg_iostat_set __percpu	*iostat_cpu;
	struct blkg_iostat_set		iostat;

	struct blkg_policy_data		*pd[BLKCG_MAX_POLS];

	spinlock_t			async_bio_lock;
	struct bio_list			async_bios;
	union {
		struct work_struct	async_bio_work;
		struct work_struct	free_work;
	};

	atomic_t			use_delay;
	atomic64_t			delay_nsec;
	atomic64_t			delay_start;
	u64				last_delay;
	int				last_use;

	struct rcu_head			rcu_head;
};

extern struct cgroup_subsys_state * const blkcg_root_css;

struct disk_stats *blkcg_dkstats_find(struct blkcg *blkcg, int cpu,
				      struct block_device *bdev, int *alloc);
struct disk_stats *blkcg_dkstats_find_create(struct blkcg *blkcg,
				      int cpu, struct block_device *bdev);

#define __blkcg_part_stat_add(blkcg, cpu, part, field, addnd)		\
({									\
	struct disk_stats *ds;						\
	ds = blkcg_dkstats_find_create((blkcg), (cpu), (part));		\
	if (ds)								\
		ds->field += (addnd);					\
})

#define blkcg_part_stat_add(blkcg, cpu, part, field, addnd)	do {	\
	__blkcg_part_stat_add((blkcg), (cpu), (part), field, (addnd));	\
} while (0)

#define blkcg_part_stat_dec(blkcg, cpu, gendiskp, field)	\
	blkcg_part_stat_add(blkcg, cpu, gendiskp, field, -1)
#define blkcg_part_stat_inc(blkcg, cpu, gendiskp, field)	\
	blkcg_part_stat_add(blkcg, cpu, gendiskp, field, 1)
#define blkcg_part_stat_sub(blkcg, cpu, gendiskp, field, subnd) \
	blkcg_part_stat_add(blkcg, cpu, gendiskp, field, -subnd)

#ifdef CONFIG_SMP
#define blkcg_part_stat_read(blkcg, part, field)			\
({									\
	typeof((part)->bd_stats->field) res = 0;				\
	unsigned int cpu;						\
	for_each_possible_cpu(cpu) {					\
		struct disk_stats *ds;					\
		ds = blkcg_dkstats_find(blkcg, cpu, part, NULL);	\
		if (!ds)						\
			break;						\
		res += ds->field;					\
	}								\
	res;								\
})
#else
#define blkcg_part_stat_read(blkcg, part, field)			\
({									\
	typeof((part)->bd_stats->field) res = 0;				\
	struct disk_stats *ds;						\
	ds = blkcg_dkstats_find(blkcg, cpu, part, NULL);		\
	if (ds)								\
		res = ds->field;					\
	res;								\
})
#endif

void blkcg_destroy_blkgs(struct blkcg *blkcg);
void blkcg_schedule_throttle(struct request_queue *q, bool use_memdelay);
void blkcg_maybe_throttle_current(void);

static inline struct blkcg *css_to_blkcg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct blkcg, css) : NULL;
}

/**
 * bio_blkcg - grab the blkcg associated with a bio
 * @bio: target bio
 *
 * This returns the blkcg associated with a bio, %NULL if not associated.
 * Callers are expected to either handle %NULL or know association has been
 * done prior to calling this.
 */
static inline struct blkcg *bio_blkcg(struct bio *bio)
{
	if (bio && bio->bi_blkg)
		return bio->bi_blkg->blkcg;
	return NULL;
}

static inline bool blk_cgroup_congested(void)
{
	struct cgroup_subsys_state *css;
	bool ret = false;

	rcu_read_lock();
	css = kthread_blkcg();
	if (!css)
		css = task_css(current, io_cgrp_id);
	while (css) {
		if (atomic_read(&css->cgroup->congestion_count)) {
			ret = true;
			break;
		}
		css = css->parent;
	}
	rcu_read_unlock();
	return ret;
}

/**
 * blkcg_parent - get the parent of a blkcg
 * @blkcg: blkcg of interest
 *
 * Return the parent blkcg of @blkcg.  Can be called anytime.
 */
static inline struct blkcg *blkcg_parent(struct blkcg *blkcg)
{
	return css_to_blkcg(blkcg->css.parent);
}

/**
 * blkcg_pin_online - pin online state
 * @blkcg: blkcg of interest
 *
 * While pinned, a blkcg is kept online.  This is primarily used to
 * impedance-match blkg and cgwb lifetimes so that blkg doesn't go offline
 * while an associated cgwb is still active.
 */
static inline void blkcg_pin_online(struct blkcg *blkcg)
{
	refcount_inc(&blkcg->online_pin);
}

/**
 * blkcg_unpin_online - unpin online state
 * @blkcg: blkcg of interest
 *
 * This is primarily used to impedance-match blkg and cgwb lifetimes so
 * that blkg doesn't go offline while an associated cgwb is still active.
 * When this count goes to zero, all active cgwbs have finished so the
 * blkcg can continue destruction by calling blkcg_destroy_blkgs().
 */
static inline void blkcg_unpin_online(struct blkcg *blkcg)
{
	do {
		if (!refcount_dec_and_test(&blkcg->online_pin))
			break;
		blkcg_destroy_blkgs(blkcg);
		blkcg = blkcg_parent(blkcg);
	} while (blkcg);
}

#ifdef CONFIG_BLK_DEV_THROTTLING
static inline uint64_t blkcg_buffered_write_bps(struct blkcg *blkcg)
{
	return blkcg->buffered_write_bps;
}

static inline unsigned long blkcg_dirty_ratelimit(struct blkcg *blkcg)
{
	return blkcg->dirty_ratelimit;
}
#endif

#else	/* CONFIG_BLK_CGROUP */

struct blkcg {
};

struct blkcg_gq {
};

#define blkcg_root_css	((struct cgroup_subsys_state *)ERR_PTR(-EINVAL))

static inline void blkcg_maybe_throttle_current(void) { }
static inline bool blk_cgroup_congested(void) { return false; }

#ifdef CONFIG_BLOCK
static inline void blkcg_schedule_throttle(struct request_queue *q, bool use_memdelay) { }
static inline struct blkcg *bio_blkcg(struct bio *bio) { return NULL; }

#define blkcg_part_stat_add(blkcg, cpu, part, field, addnd) do {} while (0)
#define blkcg_part_stat_dec(blkcg, cpu, gendiskp, field) do {} while (0)
#define blkcg_part_stat_inc(blkcg, cpu, gendiskp, field) do {} while (0)
#define blkcg_part_stat_sub(blkcg, cpu, gendiskp, field, subnd) do {} while (0)
#define blkcg_part_stat_read(blkcg, part, field) do {} while (0)

#endif /* CONFIG_BLOCK */

#endif	/* CONFIG_BLK_CGROUP */

#ifdef CONFIG_BLK_CGROUP_FC_APPID
/*
 * Sets the fc_app_id field associted to blkcg
 * @app_id: application identifier
 * @cgrp_id: cgroup id
 * @app_id_len: size of application identifier
 */
static inline int blkcg_set_fc_appid(char *app_id, u64 cgrp_id, size_t app_id_len)
{
	struct cgroup *cgrp;
	struct cgroup_subsys_state *css;
	struct blkcg *blkcg;
	int ret  = 0;

	if (app_id_len > FC_APPID_LEN)
		return -EINVAL;

	cgrp = cgroup_get_from_id(cgrp_id);
	if (!cgrp)
		return -ENOENT;
	css = cgroup_get_e_css(cgrp, &io_cgrp_subsys);
	if (!css) {
		ret = -ENOENT;
		goto out_cgrp_put;
	}
	blkcg = css_to_blkcg(css);
	/*
	 * There is a slight race condition on setting the appid.
	 * Worst case an I/O may not find the right id.
	 * This is no different from the I/O we let pass while obtaining
	 * the vmid from the fabric.
	 * Adding the overhead of a lock is not necessary.
	 */
	strlcpy(blkcg->fc_app_id, app_id, app_id_len);
	css_put(css);
out_cgrp_put:
	cgroup_put(cgrp);
	return ret;
}

/**
 * blkcg_css - find the current css
 *
 * Find the css associated with either the kthread or the current task.
 * This may return a dying css, so it is up to the caller to use tryget logic
 * to confirm it is alive and well.
 */
static inline struct cgroup_subsys_state *blkcg_css(void)
{
	struct cgroup_subsys_state *css;

	css = kthread_blkcg();
	if (css)
		return css;
	return task_css(current, io_cgrp_id);
}

static inline struct blkcg *task_blkcg(struct task_struct *tsk)
{
		return container_of(blkcg_css(), struct blkcg, css);
}

/**
 * blkcg_get_fc_appid - get the fc app identifier associated with a bio
 * @bio: target bio
 *
 * On success return the fc_app_id, on failure return NULL
 */
static inline char *blkcg_get_fc_appid(struct bio *bio)
{
	if (bio && bio->bi_blkg &&
		(bio->bi_blkg->blkcg->fc_app_id[0] != '\0'))
		return bio->bi_blkg->blkcg->fc_app_id;
	return NULL;
}
#else
static inline int blkcg_set_fc_appid(char *buf, u64 id, size_t len) { return -EINVAL; }
static inline char *blkcg_get_fc_appid(struct bio *bio) { return NULL; }
#endif /*CONFIG_BLK_CGROUP_FC_APPID*/
#endif	/* _BLK_CGROUP_H */
