/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_IMGI_H__
#define __MDP_REG_IMGI_H__

#define MDP_IMGI_DBG_SEL			0x30
#define MDP_IMGI_CG_CON				0x2000
#define MDP_IMGI_BASE_OFFSET			0x2304
#define MDP_IMGI_SMX1O_ADDR			0x27d0
#define MDP_IMGI_SMX2O_ADDR			0x2800
#define MDP_IMGI_SMX3O_ADDR			0x2830
#define MDP_IMGI_SMX4O_ADDR			0x2860
#define MDP_IMGI_SMX1I_ADDR			0x2890
#define MDP_IMGI_SMX2I_ADDR			0x28c0
#define MDP_IMGI_SMX3I_ADDR			0x28f0
#define MDP_IMGI_SMX4I_ADDR			0x2920

/* MASK */
#define MDP_IMGI_DBG_SEL_MASK			0x1fff
#define MDP_IMGI_CG_CON_MASK			0xffffffff
#define MDP_IMGI_BASE_OFFSET_MASK		0xffffffff
#define MDP_IMGI_SMX1O_ADDR_MASK		0xffffffff
#define MDP_IMGI_SMX2O_ADDR_MASK		0xffffffff
#define MDP_IMGI_SMX3O_ADDR_MASK		0xffffffff
#define MDP_IMGI_SMX4O_ADDR_MASK		0xffffffff
#define MDP_IMGI_SMX1I_ADDR_MASK		0xffffffff
#define MDP_IMGI_SMX2I_ADDR_MASK		0xffffffff
#define MDP_IMGI_SMX3I_ADDR_MASK		0xffffffff
#define MDP_IMGI_SMX4I_ADDR_MASK		0xffffffff

#endif  // __MDP_REG_IMGI_H__
