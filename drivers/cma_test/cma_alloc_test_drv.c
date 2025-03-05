// SPDX-License-Identifier: GPL-2.0
/*
 * Platform device driver to test CMA allocations
 */

#define pr_fmt(fmt) "%s: %s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/printk.h>

static const struct platform_device_id cma_test_id_table[] = {
	{ "cma_test_dev_id", 23 },
	{}, // Needs to finish with a NULL entry.
};
MODULE_DEVICE_TABLE(platform, cma_test_id_table);

// Identifies the node in the device tree.
static const struct of_device_id cma_test_dt_ids[] = {
	{ .compatible = "cma_test_dummy" },
	{}, // Needs to finish with a NULL entry.
};
MODULE_DEVICE_TABLE(of, cma_test_dt_ids);

static int cma_test_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;

	pr_info("Probing device");

	of_id = of_match_device(cma_test_dt_ids, &pdev->dev);
	if (!of_id) {
		pr_info("The node was not found in DTB");
		return -ENODEV;
	}

	return 0;
}

static void cma_test_remove(struct platform_device *pdev)
{
	pr_info("Remove cma alloc test driver");
}

static void cma_test_shutdown(struct platform_device *pdev)
{
	pr_info("Shutdown cma alloc test driver");
}

static int cma_test_suspend(struct platform_device *pdev, pm_message_t state)
{
	pr_info("Suspend cma alloc test driver");

	return 0;
}

static int cma_test_resume(struct platform_device *pdev)
{
	pr_info("Resume cma alloc test driver");

	return 0;
}

static struct platform_driver cma_test_driver = {
	.driver = {
		.name = "cma_alloc_test",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cma_test_dt_ids),
	},
	.probe = cma_test_probe,
	.remove = cma_test_remove,
	.suspend = cma_test_suspend,
	.shutdown = cma_test_shutdown,
	.resume = cma_test_resume,
	.id_table = cma_test_id_table,
};

module_platform_driver(cma_test_driver);

MODULE_AUTHOR("Juan Yescas");
MODULE_DESCRIPTION("Platform Driver to test CMA allocations");
MODULE_LICENSE("GPL v2");
