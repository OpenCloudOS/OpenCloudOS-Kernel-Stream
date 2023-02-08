// SPDX-License-Identifier: GPL-2.0-only
/*
 * Swap hook framework
 */

#include <linux/swap.h>
#include <linux/module.h>
#include <linux/swap_hook.h>

static const struct swap_hook_ops *registered_swap_hook_ops __read_mostly;

/*
 * Register operations
 */
int swap_hook_register_ops(const struct swap_hook_ops *ops)
{
	if (READ_ONCE(registered_swap_hook_ops))
		return -EINVAL;

	WRITE_ONCE(registered_swap_hook_ops, ops);

	return 0;
}
EXPORT_SYMBOL(swap_hook_register_ops);

void swap_hook_store(struct page *page)
{
	const struct swap_hook_ops *ops;

	ops = READ_ONCE(registered_swap_hook_ops);

	if (ops)
		ops->store(page);
}

void swap_hook_invalidate_page(int type, pgoff_t offset)
{
	const struct swap_hook_ops *ops;

	ops = READ_ONCE(registered_swap_hook_ops);

	if (ops)
		ops->invalidate_page(type, offset);
}

void swap_hook_invalidate_area(int type)
{
	const struct swap_hook_ops *ops;

	ops = READ_ONCE(registered_swap_hook_ops);

	if (ops)
		ops->invalidate_area(type);

	return;
}
