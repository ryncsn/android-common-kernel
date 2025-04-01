// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Google Inc.
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/thermal.h>

#define TZ_DEV_SZ_MAX 20

static struct thermal_zone_device *tz_devs[TZ_DEV_SZ_MAX];
static const char **tz_dev_names;
static u32 tz_dev_sz;

static int vtemp_get_temp(struct thermal_zone_device *tz, int *temp)
{
	int i, ret;
	int tz_temp, tz_temp_max = 0;

	for (i = 0; i < tz_dev_sz; i++) {
		if (!tz_devs[i]) {
			dev_warn(&tz->device, "Thermal zone %s is NULL\n", tz_dev_names[i]);
			continue;
		}

		ret = thermal_zone_get_temp(tz_devs[i], &tz_temp);
		if (ret < 0) {
			if (ret != -EAGAIN)
				dev_warn(&tz->device,
					 "Failed to get temp from %s: %d\n",
					 tz_dev_names[i], ret);
			continue;
		}
		tz_temp_max = max(tz_temp_max, tz_temp);
	}

	*temp = tz_temp_max;

	return 0;
}

static const struct thermal_zone_device_ops vtemp_ops = {
	.get_temp = vtemp_get_temp,
};

static int vtemp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(dev);
	struct thermal_zone_device *tz_vtemp;
	int i, ret;

	tz_dev_names = devm_kcalloc(dev, TZ_DEV_SZ_MAX, sizeof(*tz_dev_names), GFP_KERNEL);
	if (!tz_dev_names)
		return -ENOMEM;

	ret = of_property_read_string_array(np, "mediatek,tz-names", tz_dev_names, TZ_DEV_SZ_MAX);
	if (ret < 0)
		return ret;

	tz_dev_sz = (u32)ret;
	for (i = 0; i < tz_dev_sz; i++) {
		tz_devs[i] = thermal_zone_get_zone_by_name(tz_dev_names[i]);
		if (IS_ERR(tz_devs[i])) {
			ret = PTR_ERR(tz_devs[i]);

			/* The thermal zone may not be ready yet, defer probing to retry. */
			if (ret == -ENODEV) {
				dev_dbg(dev, "thermal zone %s is not ready, defer probing.\n",
					 tz_dev_names[i]);
				return -EPROBE_DEFER;
			}

			dev_err(dev, "Failed to get thermal zone %s: %d\n", tz_dev_names[i], ret);
			return ret;
		}
	}

	tz_vtemp = devm_thermal_of_zone_register(dev, 0, NULL, &vtemp_ops);
	if (IS_ERR(tz_vtemp))
		return dev_err_probe(dev, PTR_ERR(tz_vtemp),
				     "Failed to register vtemp thermal zone\n");

	return 0;
}

static const struct of_device_id vtemp_of_match[] = {
	{ .compatible = "mediatek,virtual-temp", },
	{},
};
MODULE_DEVICE_TABLE(of, vtemp_of_match);

static struct platform_driver vtemp_driver = {
	.probe = vtemp_probe,
	.driver = {
		.name = "mtk-virtual-temp",
		.of_match_table = vtemp_of_match,
	},
};

module_platform_driver(vtemp_driver);

MODULE_AUTHOR("Michael Kao <michael.kao@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Virtual Temp driver v2");
MODULE_LICENSE("GPL v2");
