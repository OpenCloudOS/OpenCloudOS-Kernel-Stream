/*
 * Copyright (C) 2014 Josh Poimboeuf <jpoimboe@redhat.com>
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * kpatch shadow variables
 *
 * These functions can be used to add new "shadow" fields to existing data
 * structures.  For example, to allocate a "newpid" variable associated with an
 * instance of task_struct, and assign it a value of 1000:
 *
 * struct task_struct *tsk = current;
 * int *newpid;
 * newpid = kpatch_shadow_alloc(tsk, "newpid", sizeof(int), GFP_KERNEL);
 * if (newpid)
 * 	*newpid = 1000;
 *
 * To retrieve a pointer to the variable:
 *
 * struct task_struct *tsk = current;
 * int *newpid;
 * newpid = kpatch_shadow_get(tsk, "newpid");
 * if (newpid)
 * 	printk("task newpid = %d\n", *newpid); // prints "task newpid = 1000"
 *
 * To free it:
 *
 * kpatch_shadow_free(tsk, "newpid");
 */

#include <linux/hashtable.h>
#include <linux/slab.h>
#include "kpatch.h"

static DEFINE_HASHTABLE(kpatch_shadow_hash, 12);
static DEFINE_SPINLOCK(kpatch_shadow_lock);

struct kpatch_shadow {
	struct hlist_node node;
	struct rcu_head rcu_head;
	void *obj;
	union {
		char *var; /* assumed to be 4-byte aligned */
		unsigned long flags;
	};
	void *data;
};

#define SHADOW_FLAG_INPLACE 0x1
#define SHADOW_FLAG_RESERVED0 0x2 /* reserved for future use */

#define SHADOW_FLAG_MASK 0x3
#define SHADOW_PTR_MASK (~(SHADOW_FLAG_MASK))

static inline void shadow_set_inplace(struct kpatch_shadow *shadow)
{
	shadow->flags |= SHADOW_FLAG_INPLACE;
}

static inline int shadow_is_inplace(struct kpatch_shadow *shadow)
{
	return shadow->flags & SHADOW_FLAG_INPLACE;
}

static inline char *shadow_var(struct kpatch_shadow *shadow)
{
	return (char *)((unsigned long)shadow->var & SHADOW_PTR_MASK);
}

void *kpatch_shadow_alloc(void *obj, char *var, size_t size, gfp_t gfp)
{
	unsigned long flags;
	struct kpatch_shadow *shadow;

	shadow = kmalloc(sizeof(*shadow), gfp);
	if (!shadow)
		return NULL;

	shadow->obj = obj;

	shadow->var = kstrdup(var, gfp);
	if (!shadow->var) {
		kfree(shadow);
		return NULL;
	}

	if (size <= sizeof(shadow->data)) {
		shadow->data = &shadow->data;
		shadow_set_inplace(shadow);
	} else {
		shadow->data = kmalloc(size, gfp);
		if (!shadow->data) {
			kfree(shadow->var);
			kfree(shadow);
			return NULL;
		}
	}

	spin_lock_irqsave(&kpatch_shadow_lock, flags);
	hash_add_rcu(kpatch_shadow_hash, &shadow->node, (unsigned long)obj);
	spin_unlock_irqrestore(&kpatch_shadow_lock, flags);

	return shadow->data;
}
EXPORT_SYMBOL_GPL(kpatch_shadow_alloc);

static void kpatch_shadow_rcu_free(struct rcu_head *head)
{
	struct kpatch_shadow *shadow;

	shadow = container_of(head, struct kpatch_shadow, rcu_head);

	if (!shadow_is_inplace(shadow))
		kfree(shadow->data);
	kfree(shadow_var(shadow));
	kfree(shadow);
}

void kpatch_shadow_free(void *obj, char *var)
{
	unsigned long flags;
	struct kpatch_shadow *shadow;

	spin_lock_irqsave(&kpatch_shadow_lock, flags);

	hash_for_each_possible(kpatch_shadow_hash, shadow, node,
			       (unsigned long)obj) {
		if (shadow->obj == obj && !strcmp(shadow_var(shadow), var)) {
			hash_del_rcu(&shadow->node);
			spin_unlock_irqrestore(&kpatch_shadow_lock, flags);
			call_rcu(&shadow->rcu_head, kpatch_shadow_rcu_free);
			return;
		}
	}

	spin_unlock_irqrestore(&kpatch_shadow_lock, flags);
}
EXPORT_SYMBOL_GPL(kpatch_shadow_free);

void *kpatch_shadow_get(void *obj, char *var)
{
	struct kpatch_shadow *shadow;

	rcu_read_lock();

	hash_for_each_possible_rcu(kpatch_shadow_hash, shadow, node,
				   (unsigned long)obj) {
		if (shadow->obj == obj && !strcmp(shadow_var(shadow), var)) {
			rcu_read_unlock();
			if (shadow_is_inplace(shadow))
				return &(shadow->data);

			return shadow->data;
		}
	}

	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL_GPL(kpatch_shadow_get);
