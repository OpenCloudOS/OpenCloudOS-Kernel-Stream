// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include <linux/nmi.h>
#include "xfs_message.h"
#include "xfs_trace.h"
#include "xfs_linux.h"

void *
kmem_alloc(size_t size, xfs_km_flags_t flags)
{
	int	retries = 0;
	gfp_t	lflags = kmem_flags_convert(flags);
	void	*ptr;

	trace_kmem_alloc(size, flags, _RET_IP_);

	if (xfs_kmem_alloc_by_vmalloc &&
			size > (PAGE_SIZE * xfs_kmem_alloc_by_vmalloc) &&
			xfs_kmem_alloc_large_dump_stack) {
		xfs_warn(NULL, "%s size: %ld large than %ld\n",
				__func__, size, PAGE_SIZE * xfs_kmem_alloc_by_vmalloc);
		dump_stack();
	}

	do {
		if (xfs_kmem_alloc_by_vmalloc && (size > PAGE_SIZE * xfs_kmem_alloc_by_vmalloc))
			ptr = __vmalloc(size, lflags);
		else
			ptr = kmalloc(size, lflags);
		if (ptr || (flags & KM_MAYFAIL))
			return ptr;
		if (!(++retries % 100)) {
			xfs_err(NULL,
				"%s(%u) possible memory allocation deadlock size %u in %s (mode:0x%x), flags: 0x%x",
				current->comm, current->pid,
				(unsigned int)size, __func__, lflags, flags);
			if (xfs_kmem_fail_dump_stack == 1)
				dump_stack();
			else if (xfs_kmem_fail_dump_stack == 2)
				trigger_all_cpu_backtrace();
			else if (xfs_kmem_fail_dump_stack == 3)
				__show_mem(0, NULL, gfp_zone(lflags));
		}
		memalloc_retry_wait(lflags);
	} while (1);
}
