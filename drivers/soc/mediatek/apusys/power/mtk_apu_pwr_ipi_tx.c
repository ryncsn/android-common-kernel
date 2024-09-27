// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/completion.h>
#include <linux/cleanup.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/rpmsg.h>
#include <linux/thermal.h>

#include "mtk_apu_pwr_ipi.h"

struct mtk_apu_pwr_rpmsg {
	struct device *dev;
	struct rpmsg_device *rpdev;
	struct mutex send_lock;
	struct completion comp;
	unsigned int curr_freq;
	enum mtk_apu_pwr_cmd curr_rpmsg_cmd;
};

static int mtk_apu_pwr_send_rpmsg(struct device *dev, struct mtk_apu_pwr_rpmsg_data *rpmsg_data,
				  unsigned int timeout)
{
	struct mtk_apu_pwr_rpmsg *power_rpmsg = dev_get_drvdata(dev);

	scoped_guard(mutex, &power_rpmsg->send_lock) {
		long ret;

		reinit_completion(&power_rpmsg->comp);

		power_rpmsg->curr_rpmsg_cmd = rpmsg_data->cmd;

		ret = rpmsg_send(power_rpmsg->rpdev->ept, rpmsg_data, sizeof(*rpmsg_data));
		if (ret) {
			dev_err(dev, "%s: failed to send msg to remote side, ret=%ld\n",
				__func__, ret);
			return ret;
		}

		if (!timeout)
			return 0;

		ret = wait_for_completion_interruptible_timeout(&power_rpmsg->comp,
								msecs_to_jiffies(timeout));

		if (ret < 0) {
			dev_err(dev, "%s: failed to wait for completion, ret=%ld\n",
				__func__, ret);
			return ret;
		} else if (ret == 0) {
			dev_err(dev, "%s: failed to wait for completion, timeout\n", __func__);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int mtk_apu_pwr_tx_callback(struct rpmsg_device *rpdev, void *data,
				   int len, void *priv, u32 src)
{
	struct device *dev = &rpdev->dev;
	struct mtk_apu_pwr_rpmsg *power_rpmsg = dev_get_drvdata(dev);
	struct mtk_apu_pwr_rpmsg_data *rpmsg_data;

	if (len != sizeof(*rpmsg_data)) {
		dev_err(dev, "%s: invalid length %d, which should be %zu\n", __func__, len,
			sizeof(*rpmsg_data));
		return -EINVAL;
	}

	rpmsg_data = data;

	if (rpmsg_data->cmd != power_rpmsg->curr_rpmsg_cmd) {
		dev_err(dev, "%s: the current command is %d, abort unexpected command %d\n",
			__func__, power_rpmsg->curr_rpmsg_cmd, rpmsg_data->cmd);
		return -EINVAL;
	}

	switch (power_rpmsg->curr_rpmsg_cmd) {
	case MTK_APU_PWR_GET_CURR_FREQ:
		power_rpmsg->curr_freq = rpmsg_data->data0;
		break;
	default:
		break;
	}

	complete(&power_rpmsg->comp);

	return 0;
}

static int mtk_apu_set_limit_freq(struct device *dev, int max_freq)
{
	struct mtk_apu_pwr_rpmsg_data rpmsg_data = {
		.cmd = MTK_APU_PWR_SET_LIMIT_FREQ,
		.data0 = max_freq,
	};

	return mtk_apu_pwr_send_rpmsg(dev, &rpmsg_data, 100);
}

static int mtk_apu_get_remote_freq(struct device *dev)
{
	struct mtk_apu_pwr_rpmsg_data rpmsg_data = {
		.cmd = MTK_APU_PWR_GET_CURR_FREQ,
	};

	return mtk_apu_pwr_send_rpmsg(dev, &rpmsg_data, 100);
}

static int mtk_apu_freq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct dev_pm_opp *opp;
	unsigned long rate;

	flags |= DEVFREQ_FLAG_LEAST_UPPER_BOUND;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		dev_err(dev, "%s: failed to get recommended opp for %lu hz\n", __func__, *freq);
		return PTR_ERR(opp);
	}
	dev_pm_opp_put(opp);

	rate = *freq / 1000; /* to KHz */

	return mtk_apu_set_limit_freq(dev, rate);
}

static int mtk_apu_get_cur_freq(struct device *dev, unsigned long *freq)
{
	int ret;
	struct mtk_apu_pwr_rpmsg *power_rpmsg = dev_get_drvdata(dev);

	ret = mtk_apu_get_remote_freq(dev);

	if (ret) {
		dev_err(dev, "Failed to get current frequency, ret = %d\n", ret);
		*freq = 0;
		return ret;
	}

	*freq = power_rpmsg->curr_freq * 1000;

	return ret;
}

static int mtk_apu_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	/*
	 *	MediaTek NPU does not support dynamic adjustment of OPP level
	 *	on the kernel side, so this function is bypassed.
	 */
	return 0;
}

static struct devfreq_dev_profile devfreq_dev_profile = {
	.initial_freq = 1800000000,
	.polling_ms = 0,
	.target = mtk_apu_freq_target,
	.get_dev_status = mtk_apu_get_dev_status,
	.get_cur_freq = mtk_apu_get_cur_freq,
	.is_cooling_device	= true,
};

static int mtk_apu_pwr_tx_probe(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct mtk_apu_pwr_rpmsg *power_rpmsg;
	struct devfreq *devfreq;
	int ret = 0;

	power_rpmsg = devm_kzalloc(dev, sizeof(struct mtk_apu_pwr_rpmsg), GFP_KERNEL);
	if (!power_rpmsg)
		return -ENOMEM;

	power_rpmsg->rpdev = rpdev;
	init_completion(&power_rpmsg->comp);
	mutex_init(&power_rpmsg->send_lock);

	power_rpmsg->dev = dev;
	dev_set_drvdata(dev, power_rpmsg);

	ret = dev_pm_opp_of_add_table(dev);
	if (ret)
		dev_err(dev, "%s: failed to add opp table (%d)\n", __func__, ret);

	devfreq = devm_devfreq_add_device(dev, &devfreq_dev_profile, DEVFREQ_GOV_PERFORMANCE,
					  NULL);
	if (IS_ERR(devfreq))
		dev_err(dev, "%s: failed to register devfreq (%ld)\n", __func__, PTR_ERR(devfreq));
	else
		dev_info(dev, "%s: registered devfreq\n", __func__);

	return 0;
}

static const struct of_device_id mtk_apu_pwr_tx_rpmsg_of_match[] = {
	{ .compatible = "mediatek,aputop-rpmsg", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, mtk_apu_pwr_tx_rpmsg_of_match);

static struct rpmsg_driver apu_pwr_tx_rpmsg_drv = {
	.drv	= {
		.name	= "mtk_apu_pwr_ipi_tx",
		.owner = THIS_MODULE,
		.of_match_table = mtk_apu_pwr_tx_rpmsg_of_match,
	},
	.callback = mtk_apu_pwr_tx_callback,
	.probe = mtk_apu_pwr_tx_probe,
};

module_rpmsg_driver(apu_pwr_tx_rpmsg_drv);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek apu-tx-ipi rpmsg driver");
