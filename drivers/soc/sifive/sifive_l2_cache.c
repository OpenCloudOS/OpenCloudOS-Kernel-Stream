// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive L2 cache controller Driver
 *
 * Copyright (C) 2018-2019 SiFive, Inc.
 *
 */
#include <linux/align.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <asm/cacheinfo.h>
#include <asm/page.h>
#include <soc/sifive/sifive_l2_cache.h>

#define SIFIVE_L2_DIRECCFIX_LOW 0x100
#define SIFIVE_L2_DIRECCFIX_HIGH 0x104
#define SIFIVE_L2_DIRECCFIX_COUNT 0x108

#define SIFIVE_L2_DIRECCFAIL_LOW 0x120
#define SIFIVE_L2_DIRECCFAIL_HIGH 0x124
#define SIFIVE_L2_DIRECCFAIL_COUNT 0x128

#define SIFIVE_L2_DATECCFIX_LOW 0x140
#define SIFIVE_L2_DATECCFIX_HIGH 0x144
#define SIFIVE_L2_DATECCFIX_COUNT 0x148

#define SIFIVE_L2_DATECCFAIL_LOW 0x160
#define SIFIVE_L2_DATECCFAIL_HIGH 0x164
#define SIFIVE_L2_DATECCFAIL_COUNT 0x168

#define SIFIVE_L2_FLUSH64 0x200
#define SIFIVE_L2_FLUSH32 0x240

#define SIFIVE_L2_CONFIG 0x00
#define SIFIVE_L2_WAYENABLE 0x08
#define SIFIVE_L2_ECCINJECTERR 0x40

#define SIFIVE_L2_MAX_ECCINTR 4
#define SIFIVE_L2_LINE_SIZE 64

static void __iomem *l2_base;
static int g_irq[SIFIVE_L2_MAX_ECCINTR];
static struct riscv_cacheinfo_ops l2_cache_ops;
static phys_addr_t uncached_offset;
DEFINE_STATIC_KEY_FALSE(sifive_l2_handle_noncoherent_key);

enum {
	DIR_CORR = 0,
	DATA_CORR,
	DATA_UNCORR,
	DIR_UNCORR,
};

#ifdef CONFIG_DEBUG_FS
static struct dentry *sifive_test;

static ssize_t l2_write(struct file *file, const char __user *data,
			size_t count, loff_t *ppos)
{
	unsigned int val;

	if (kstrtouint_from_user(data, count, 0, &val))
		return -EINVAL;
	if ((val < 0xFF) || (val >= 0x10000 && val < 0x100FF))
		writel(val, l2_base + SIFIVE_L2_ECCINJECTERR);
	else
		return -EINVAL;
	return count;
}

static const struct file_operations l2_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = l2_write
};

static void setup_sifive_debug(void)
{
	sifive_test = debugfs_create_dir("sifive_l2_cache", NULL);

	debugfs_create_file("sifive_debug_inject_error", 0200,
			    sifive_test, NULL, &l2_fops);
}
#endif

static void l2_config_read(void)
{
	u32 regval, val;

	regval = readl(l2_base + SIFIVE_L2_CONFIG);
	val = regval & 0xFF;
	pr_info("L2CACHE: No. of Banks in the cache: %d\n", val);
	val = (regval & 0xFF00) >> 8;
	pr_info("L2CACHE: No. of ways per bank: %d\n", val);
	val = (regval & 0xFF0000) >> 16;
	pr_info("L2CACHE: Sets per bank: %llu\n", (uint64_t)1 << val);
	val = (regval & 0xFF000000) >> 24;
	pr_info("L2CACHE: Bytes per cache block: %llu\n", (uint64_t)1 << val);

	regval = readl(l2_base + SIFIVE_L2_WAYENABLE);
	pr_info("L2CACHE: Index of the largest way enabled: %d\n", regval);
}

static ATOMIC_NOTIFIER_HEAD(l2_err_chain);

int register_sifive_l2_error_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&l2_err_chain, nb);
}
EXPORT_SYMBOL_GPL(register_sifive_l2_error_notifier);

int unregister_sifive_l2_error_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&l2_err_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_sifive_l2_error_notifier);

void sifive_l2_flush_range(phys_addr_t start, size_t len)
{
	phys_addr_t end = start + len;
	phys_addr_t line;

	if (!len)
		return;

	mb();
	for (line = ALIGN_DOWN(start, SIFIVE_L2_LINE_SIZE); line < end;
			line += SIFIVE_L2_LINE_SIZE) {
#ifdef CONFIG_32BIT
		writel(line >> 4, l2_base + SIFIVE_L2_FLUSH32);
#else
		writeq(line, l2_base + SIFIVE_L2_FLUSH64);
#endif
		mb();
	}
}
EXPORT_SYMBOL_GPL(sifive_l2_flush_range);

void *sifive_l2_set_uncached(void *addr, size_t size)
{
	phys_addr_t phys_addr = __pa(addr) + uncached_offset;
	void *mem_base;

	mem_base = memremap(phys_addr, size, MEMREMAP_WT);
	if (!mem_base) {
		pr_err("%s memremap failed for addr %p\n", __func__, addr);
		return ERR_PTR(-EINVAL);
	}

	return mem_base;
}
EXPORT_SYMBOL_GPL(sifive_l2_set_uncached);

static int l2_largest_wayenabled(void)
{
	return readl(l2_base + SIFIVE_L2_WAYENABLE) & 0xFF;
}

static ssize_t number_of_ways_enabled_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return sprintf(buf, "%u\n", l2_largest_wayenabled());
}

static DEVICE_ATTR_RO(number_of_ways_enabled);

static struct attribute *priv_attrs[] = {
	&dev_attr_number_of_ways_enabled.attr,
	NULL,
};

static const struct attribute_group priv_attr_group = {
	.attrs = priv_attrs,
};

static const struct attribute_group *l2_get_priv_group(struct cacheinfo *this_leaf)
{
	/* We want to use private group for L2 cache only */
	if (this_leaf->level == 2)
		return &priv_attr_group;
	else
		return NULL;
}

static irqreturn_t l2_int_handler(int irq, void *device)
{
	unsigned int add_h, add_l;

	if (irq == g_irq[DIR_CORR]) {
		add_h = readl(l2_base + SIFIVE_L2_DIRECCFIX_HIGH);
		add_l = readl(l2_base + SIFIVE_L2_DIRECCFIX_LOW);
		pr_err("L2CACHE: DirError @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DirError interrupt sig */
		readl(l2_base + SIFIVE_L2_DIRECCFIX_COUNT);
		atomic_notifier_call_chain(&l2_err_chain, SIFIVE_L2_ERR_TYPE_CE,
					   "DirECCFix");
	}
	if (irq == g_irq[DIR_UNCORR]) {
		add_h = readl(l2_base + SIFIVE_L2_DIRECCFAIL_HIGH);
		add_l = readl(l2_base + SIFIVE_L2_DIRECCFAIL_LOW);
		/* Reading this register clears the DirFail interrupt sig */
		readl(l2_base + SIFIVE_L2_DIRECCFAIL_COUNT);
		atomic_notifier_call_chain(&l2_err_chain, SIFIVE_L2_ERR_TYPE_UE,
					   "DirECCFail");
		panic("L2CACHE: DirFail @ 0x%08X.%08X\n", add_h, add_l);
	}
	if (irq == g_irq[DATA_CORR]) {
		add_h = readl(l2_base + SIFIVE_L2_DATECCFIX_HIGH);
		add_l = readl(l2_base + SIFIVE_L2_DATECCFIX_LOW);
		pr_err("L2CACHE: DataError @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DataError interrupt sig */
		readl(l2_base + SIFIVE_L2_DATECCFIX_COUNT);
		atomic_notifier_call_chain(&l2_err_chain, SIFIVE_L2_ERR_TYPE_CE,
					   "DatECCFix");
	}
	if (irq == g_irq[DATA_UNCORR]) {
		add_h = readl(l2_base + SIFIVE_L2_DATECCFAIL_HIGH);
		add_l = readl(l2_base + SIFIVE_L2_DATECCFAIL_LOW);
		pr_err("L2CACHE: DataFail @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DataFail interrupt sig */
		readl(l2_base + SIFIVE_L2_DATECCFAIL_COUNT);
		atomic_notifier_call_chain(&l2_err_chain, SIFIVE_L2_ERR_TYPE_UE,
					   "DatECCFail");
	}

	return IRQ_HANDLED;
}

static int __init sifive_l2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned long quirks = (uintptr_t)device_get_match_data(dev);
	u64 offset;
	int nirqs;
	int ret;
	int i;

	l2_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(l2_base))
		return PTR_ERR(l2_base);

	nirqs = platform_irq_count(pdev);
	if (nirqs <= 0)
		return dev_err_probe(dev, -ENODEV, "no interrupts\n");

	for (i = 0; i < nirqs; i++) {
		g_irq[i] = platform_get_irq(pdev, i);
		if (g_irq[i] < 0)
			return g_irq[i];

		if (quirks & BIT(i))
			continue;

		ret = devm_request_irq(dev, g_irq[i], l2_int_handler, 0, pdev->name, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "Could not request IRQ %d\n", g_irq[i]);
	}

	if (!device_property_read_u64(dev, "uncached-offset", &offset)) {
		uncached_offset = offset;
		static_branch_enable(&sifive_l2_handle_noncoherent_key);
	}

	l2_config_read();

	l2_cache_ops.get_priv_group = l2_get_priv_group;
	riscv_set_cacheinfo_ops(&l2_cache_ops);

#ifdef CONFIG_DEBUG_FS
	setup_sifive_debug();
#endif
	return 0;
}

static const struct of_device_id sifive_l2_match[] = {
	{ .compatible = "sifive,fu540-c000-ccache" },
	{ .compatible = "sifive,fu740-c000-ccache" },
	{ .compatible = "starfive,jh7100-ccache", .data = (void *)BIT(DATA_UNCORR) },
	{ /* sentinel */ }
};

static struct platform_driver sifive_l2_driver = {
	.driver = {
		.name = "sifive_l2_cache",
		.of_match_table = sifive_l2_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(sifive_l2_driver, sifive_l2_probe);
