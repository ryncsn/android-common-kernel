// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/arm-smccc.h>
#include <linux/kernel.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/firmware/mediatek/mtk-apu.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mailbox/mtk-apu-mailbox.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>

#include <linux/remoteproc/mtk_apu.h>
#include <linux/remoteproc/mtk_apu_loadimage.h>
#include <linux/soc/mediatek/mtk_apu_pwr.h>
#include "../mtk_apu_pwr_ipi.h"
#include "../mtk_apu_top.h"

/* RPC offset define */
#define MTK_APU_RPC_STATUS_1		(0x0034)
#define MTK_APU_RPC_INTF_PWR_RDY	(0x0044)
#define MTK_APU_RPC_PWR_ACK		(0x0048)

/* vcore offset define */
#define MTK_APU_VCORE_CG_CLR		(0x0008)

/* rcx offset define */
#define MTK_APU_RCX_CG_CLR		(0x0008)

enum apupw_reg {
	RCX,
	VCORE,
	RPC,
	APUPW_MAX_REGS,
};

struct mtk_apu_power {
	void __iomem *regs[APUPW_MAX_REGS];
	struct regmap_field *apu_smmu_sema_regmap_field;
};

static const char *reg_name[APUPW_MAX_REGS] = {
	"RCX", "VCORE", "RPC"
};

static struct regmap *get_mbox_regmap(struct device *dev)
{
	struct device_node *mbox_node;
	struct platform_device *mbox_pdev;
	struct regmap *regmap = NULL;
	struct device_link *link;

	mbox_node = of_parse_phandle(dev->of_node, "mediatek,mbox-spare-reg", 0);
	if (!mbox_node)
		return ERR_PTR(-ENODEV);

	mbox_pdev = of_find_device_by_node(mbox_node);
	if (!mbox_pdev) {
		regmap = ERR_PTR(-EPROBE_DEFER);
		goto out_put_node;
	}

	regmap = dev_get_regmap(&mbox_pdev->dev, NULL);
	if (!regmap)
		regmap = ERR_PTR(-EINVAL);

	link = device_link_add(dev, &mbox_pdev->dev, DL_FLAG_PM_RUNTIME);
	if (!link)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "failed to add device link between power and mailbox\n");

	platform_device_put(mbox_pdev);

out_put_node:
	of_node_put(mbox_node);
	return regmap;
}

static int init_reg_base(struct platform_device *pdev, struct mtk_apu_power *apupw)
{
	for (unsigned int idx = 0; idx < ARRAY_SIZE(reg_name); idx++) {
		apupw->regs[idx] = devm_platform_ioremap_resource_byname(pdev, reg_name[idx]);
		if (IS_ERR(apupw->regs[idx]))
			return dev_err_probe(&pdev->dev, PTR_ERR(apupw->regs[idx]),
					     "%s remap base fail\n", reg_name[idx]);
	}

	return 0;
}

static uint32_t apusys_pwr_smc_call(struct device *dev, uint32_t smc_id, uint32_t a2)
{
	struct arm_smccc_res res;

	dev_dbg(dev, "%s: smc call %d\n", __func__, smc_id);

	arm_smccc_smc(MTK_SIP_APUSYS_CONTROL, smc_id, a2, 0, 0, 0, 0, 0, &res);
	if (((long) res.a0) < 0)
		dev_err(dev, "%s: smc call %d return error(%lu)\n", __func__, smc_id, res.a0);

	return res.a0;
}


static int get_reserved_mem(struct device *dev, void **resv_mem_va)
{
	struct device_node *data_np;
	struct resource r;
	int ret;

	/*
	 * To enable the MTK APU power feature, we need to load the binary of the
	 * Command Engine (CE) into the reserved memory, which is shared with the
	 * MTK APU remote processor (remoteproc) to store the APU image.
	 * Therefore, we need to parse the phandle and obtain the virtual address
	 * of that reserved memory.
	 */
	data_np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!data_np) {
		dev_err(dev, "No reserved memory region found.\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(data_np, 0, &r);
	if (ret) {
		dev_err(dev, "failed to parse reserved memory: %d\n", ret);
		of_node_put(data_np);
		return ret;
	}
	of_node_put(data_np);

	*resv_mem_va = devm_ioremap_wc(dev, r.start, resource_size(&r));

	return 0;
}

static int load_ce_bin(struct device *dev, const struct firmware *fw, void *resv_mem_va)
{
	struct apusys_secure_info_t *sec_info_ptr = NULL;
	const struct ptimg_hdr_t *hdr;
	const struct ptimg_hdr_t *hdr_end;
	const void *img;
	void *resv_ce_va;

	hdr = (const struct ptimg_hdr_t *)(fw->data + 0x200);
	dev_dbg(dev, "%s: hdr->magic is 0x%x\n", __func__, hdr->magic);

	sec_info_ptr = resv_mem_va + 0x100000;
	sec_info_ptr->ce_bin_ofs = 0x100000 + roundup(sizeof(*sec_info_ptr), 16);
	resv_ce_va = resv_mem_va + sec_info_ptr->ce_bin_ofs;

	hdr_end = (const struct ptimg_hdr_t *)(fw->data + fw->size);
	while (hdr < hdr_end && hdr->magic == PT_MAGIC) {
		img = ((const void *) hdr) + hdr->hdr_size;
		dev_dbg(dev, "Rhdr->hdr_size= 0x%x\n", hdr->hdr_size);
		dev_dbg(dev, "img address is %p\n", img);

		switch (hdr->id) {
		case PT_ID_CE_BIN:
			dev_dbg(dev, "PT_ID_CE_BIN\n");
			sec_info_ptr->ce_bin_sz = hdr->hdr_size + hdr->img_size;
			memcpy_toio(resv_ce_va, img, sec_info_ptr->ce_bin_sz);
			dev_dbg(dev, "ce_bin_sz = 0x%x\n", sec_info_ptr->ce_bin_sz);
			return 0;
		default:
			break;
		}

		img += roundup(hdr->img_size, hdr->align);
		hdr = (const struct ptimg_hdr_t *)img;
	}

	return -EINVAL;
}

static int setup_ce_bin(struct device *dev, const char *fw_path)
{
	const struct firmware *fw;
	int ret;
	void *resv_mem_va;

	ret = request_firmware(&fw, fw_path, dev);
	if (ret < 0) {
		dev_err(dev, "failed to load firmware '%s': %d\n", fw_path, ret);
		return ret;
	}

	ret = get_reserved_mem(dev, &resv_mem_va);
	if (ret)
		goto out_release_fw;

	ret = load_ce_bin(dev, fw, resv_mem_va);
	if (ret)
		goto out_unmap_mem;

	ret = apusys_pwr_smc_call(dev, MTK_APUSYS_KERNEL_OP_APUSYS_SETUP_CE_BIN, 0);
	if (ret)
		dev_err(dev, "%s: load ce bin failed, ret = %d\n", __func__, ret);

out_unmap_mem:
	devm_iounmap(dev, resv_mem_va);

out_release_fw:
	release_firmware(fw);

	return ret;
}

static int __apu_wake_rpc_rcx(struct device *dev, struct mtk_apu_power *apupw)
{
	int ret = 0, val = 0;

	dev_dbg(dev, "%s before wakeup RCX MTK_APU_RPC_INTF_PWR_RDY = 0x%x\n", __func__,
		readl(apupw->regs[RPC] + MTK_APU_RPC_INTF_PWR_RDY));

	/* Used RV SMC call to wake up RPC */
	apusys_pwr_smc_call(dev, MTK_APUSYS_KERNEL_OP_APUSYS_PWR_TOP_ON, 0);

	ret = readl_relaxed_poll_timeout_atomic((apupw->regs[RPC] + MTK_APU_RPC_INTF_PWR_RDY),
			val, (val & 0x1UL), 50, 10000);

	if (ret) {
		dev_err(dev, "%s polling RPC RDY timeout, ret %d\n", __func__, ret);
		dev_err(dev, "%s RCX MTK_APU_RPC_PWR_ACK = 0x%x\n", __func__,
			readl(apupw->regs[RPC] + MTK_APU_RPC_PWR_ACK));
		goto out;
	}

	dev_dbg(dev, "%s after wakeup RCX MTK_APU_RPC_INTF_PWR_RDY = 0x%x\n", __func__,
		readl(apupw->regs[RPC] + MTK_APU_RPC_INTF_PWR_RDY));

	ret |= readl_relaxed_poll_timeout_atomic((apupw->regs[RPC] + MTK_APU_RPC_STATUS_1),
			val, (val & (0x1 << 13)), 50, 10000);

	if (ret) {
		dev_err(dev, "%s polling ARE FSM timeout, ret %d\n", __func__, ret);
		dev_err(dev, "%s RCX MTK_APU_RPC_PWR_ACK 0x%x\n", __func__,
			 readl(apupw->regs[RPC] + MTK_APU_RPC_PWR_ACK));
		goto out;
	}

	/* clear vcore/rcx cgs */
	writel(0xFFFFFFFF, apupw->regs[VCORE] + MTK_APU_VCORE_CG_CLR);
	writel(0xFFFFFFFF, apupw->regs[RCX] + MTK_APU_RCX_CG_CLR);

out:
	return ret;
}

static int mt8196_apu_top_on(struct device *dev, struct mtk_apu_power *apupw)
{
	int ret;

	ret = __apu_wake_rpc_rcx(dev, apupw);
	if (ret) {
		dev_err(dev, "%s fail to wakeup RPC, ret %d\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int mt8196_apu_top_probe(struct platform_device *pdev)
{
	int ret = 0, val = 0;
	struct device *dev = &pdev->dev;
	struct regmap *mbox_regmap;
	struct mtk_apu_power *apupw;
	const char *fw_path = "mediatek/mt8196/apusys.img";

	ret = setup_ce_bin(dev, fw_path);
	if (ret)
		return ret;

	apupw = devm_kzalloc(dev, sizeof(*apupw), GFP_KERNEL);
	if (!apupw)
		return -ENOMEM;

	ret = init_reg_base(pdev, apupw);
	if (ret)
		return ret;

	mbox_regmap = get_mbox_regmap(dev);
	if (IS_ERR(mbox_regmap)) {
		ret = PTR_ERR(mbox_regmap);
		return dev_err_probe(dev, ret, "failed to get mailbox regmap: %d\n", ret);
	}

	ret = mt8196_apu_top_on(dev, apupw);
	if (ret)
		return dev_err_probe(dev, ret, "fail to power on APU\n");

	ret = readl_relaxed_poll_timeout_atomic((apupw->regs[RPC] + MTK_APU_RPC_INTF_PWR_RDY),
			val, (val & 0x1UL), 50, 10000);
	if (ret)
		return dev_err_probe(dev, ret, "fail to wait for RPC ready\n");

	/* release hw sema before smmu driver init */
	dev_dbg(dev, "%s release hw sema before smmu driver init\n", __func__);
	ret = regmap_write(mbox_regmap, 0xA0, 0x2);
	if (ret)
		return dev_err_probe(dev, ret, "failed to release hw sema (%d)\n", ret);

	dev_set_drvdata(dev, apupw);

	return ret;
}

static int mt8196_get_rpc_status(struct platform_device *pdev)
{
	struct mtk_apu_power *apupw = dev_get_drvdata(&pdev->dev);

	return readl(apupw->regs[RPC] + MTK_APU_RPC_STATUS_1);
}

static int mt8196_get_rpc_pwr_status(struct platform_device *pdev)
{
	struct mtk_apu_power *apupw = dev_get_drvdata(&pdev->dev);

	return readl(apupw->regs[RPC] + MTK_APU_RPC_INTF_PWR_RDY);
}

const struct apupwr_plat_data mt8196_plat_data = {
	.probe = mt8196_apu_top_probe,
	.get_rpc_status = mt8196_get_rpc_status,
	.get_rpc_pwr_status = mt8196_get_rpc_pwr_status,
};
