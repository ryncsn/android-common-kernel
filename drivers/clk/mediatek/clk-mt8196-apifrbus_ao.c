// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 MediaTek Inc.
 * Author: Guangjie Song <guangjie.song@mediatek.com>
 */

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8196-clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs ifr_mem_cg_regs = {
	.set_ofs = 0xd04,
	.clr_ofs = 0xd08,
	.sta_ofs = 0xd00,
};

#define GATE_IFR_MEM(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ifr_mem_cg_regs,		\
		.shift = _shift,			\
		.flags = CLK_OPS_PARENT_ENABLE,		\
		.ops = &mtk_clk_gate_ops_setclr,	\
	}

#define GATE_IFR_MEM_V(_id, _name, _parent) {	\
		.id = _id,			\
		.name = _name,			\
		.parent_name = _parent,		\
		.regs = &cg_regs_dummy,		\
		.ops = &mtk_clk_dummy_ops,	\
	}

static const struct mtk_gate ifr_mem_clks[] = {
	GATE_IFR_MEM(CLK_IFR_MEM_DPMAIF_MAIN, "ifr_mem_dpmaif_main",
			"ck_dpmaif_main_ck", 2),
	GATE_IFR_MEM_V(CLK_IFR_MEM_DPMAIF_MAIN_CCCI, "ifr_mem_dpmaif_main_ccci",
			"ifr_mem_dpmaif_main"),
	GATE_IFR_MEM(CLK_IFR_MEM_DPMAIF_26M, "ifr_mem_dpmaif_26m",
			"vlp_infra_26m_ck", 3),
	GATE_IFR_MEM_V(CLK_IFR_MEM_DPMAIF_26M_CCCI, "ifr_mem_dpmaif_26m_ccci",
			"ifr_mem_dpmaif_26m"),
};

static const struct mtk_clk_desc ifr_mem_mcd = {
	.clks = ifr_mem_clks,
	.num_clks = ARRAY_SIZE(ifr_mem_clks),
};

static const struct of_device_id of_match_clk_mt8196_apifrbus_ao[] = {
	{ .compatible = "mediatek,mt8196-apifrbus_ao_mem_reg", .data = &ifr_mem_mcd, },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt8196_apifrbus_ao_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8196-apifrbus_ao",
		.of_match_table = of_match_clk_mt8196_apifrbus_ao,
	},
};

module_platform_driver(clk_mt8196_apifrbus_ao_drv);
MODULE_LICENSE("GPL");
