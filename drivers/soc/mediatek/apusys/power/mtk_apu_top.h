/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 MediaTek Inc.
 */
#ifndef __MTK_APU_TOP_H__
#define __MTK_APU_TOP_H__

struct platform_device;

struct apupwr_plat_data {
	int (*probe)(struct platform_device *pdev);
	void (*remove)(struct platform_device *pdev);
	int (*get_rpc_status)(struct platform_device *pdev);
	int (*get_rpc_pwr_status)(struct platform_device *pdev);
};

extern const struct apupwr_plat_data mt8196_plat_data;

#endif
