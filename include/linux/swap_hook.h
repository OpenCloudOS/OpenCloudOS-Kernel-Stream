/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAP_HOOK_H
#define _LINUX_SWAP_HOOK_H

#include <linux/swap.h>
#include <linux/mm.h>

#ifdef CONFIG_SWAP_HOOK

struct swap_hook_ops {
	void (*store)(struct page *); /* store a page */
	void (*invalidate_page)(unsigned, pgoff_t); /* page no longer needed */
	void (*invalidate_area)(unsigned); /* page no longer needed */
};

extern int swap_hook_register_ops(const struct swap_hook_ops *ops);
extern void swap_hook_store(struct page *page);
extern void swap_hook_invalidate_page(int type, pgoff_t offset);
extern void swap_hook_invalidate_area(int type);

#else

static void inline swap_hook_store(struct page *page)
{
	return;
}
static void inline swap_hook_invalidate_page(int type, pgoff_t offset)
{
	return;
}
static void inline swap_hook_invalidate_area(int type)
{
	return;
}

#endif

#endif /* _LINUX_FRONTSWAP_H */
