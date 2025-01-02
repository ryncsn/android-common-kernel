/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef MTK_APU_LOADIMAGE_H
#define MTK_APU_LOADIMAGE_H

#define APUSYS_FW_ALIGN			(16)
#define MTK_APUSYS_SEC_MEM_ALIGN	(0x200000)
#define PT_MAGIC			(0x58901690)

struct ptimg_hdr_t {
	unsigned int magic;	/* magic number*/
	unsigned int hdr_size;	/* header size */
	unsigned int img_size;	/* img size */
	unsigned int align;	/* alignment */
	unsigned int id;	/* image id */
	unsigned int addr;	/* memory addr */
};

enum PT_ID_APUSYS {
	PT_ID_APUSYS_FW,
	PT_ID_APUSYS_XFILE,
	PT_ID_MDLA_FW_BOOT,
	PT_ID_MDLA_FW_MAIN,
	PT_ID_MDLA_XFILE,
	PT_ID_MVPU_FW,
	PT_ID_MVPU_XFILE,
	PT_ID_MVPU_SEC_FW,
	PT_ID_MVPU_SEC_XFILE,
	PT_ID_CE_BIN,
	PT_ID_CE_BIN_DUMMY
};

#endif /* MTK_APU_LOADIMAGE_H */
