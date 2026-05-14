// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2025 Google LLC
 */

#include "gve.h"
#include "gve_adminq.h"

/* Interval to schedule a nic timestamp calibration, 250ms. */
#define GVE_NIC_TS_SYNC_INTERVAL_MS 250

/* Read the nic timestamp from hardware via the admin queue. */
static int gve_clock_nic_ts_read(struct gve_ptp *ptp, u64 *nic_raw)
{
	int err;

	mutex_lock(&ptp->nic_ts_read_lock);
	err = gve_adminq_report_nic_ts(ptp->priv, ptp->nic_ts_report_bus);
	if (err)
		goto out;

	*nic_raw = be64_to_cpu(ptp->nic_ts_report->nic_timestamp);

out:
	mutex_unlock(&ptp->nic_ts_read_lock);
	return err;
}

static int gve_ptp_gettimex64(struct ptp_clock_info *info,
			      struct timespec64 *ts,
			      struct ptp_system_timestamp *sts)
{
	return -EOPNOTSUPP;
}

static int gve_ptp_settime64(struct ptp_clock_info *info,
			     const struct timespec64 *ts)
{
	return -EOPNOTSUPP;
}

static long gve_ptp_do_aux_work(struct ptp_clock_info *info)
{
	struct gve_ptp *ptp = container_of(info, struct gve_ptp, info);
	struct gve_priv *priv = ptp->priv;
	u64 nic_raw;
	int err;

	if (gve_get_reset_in_progress(priv) || !gve_get_admin_queue_ok(priv))
		goto out;

	err = gve_clock_nic_ts_read(ptp, &nic_raw);
	if (err) {
		dev_err_ratelimited(&priv->pdev->dev, "%s read err %d\n",
				    __func__, err);
		goto out;
	}
	WRITE_ONCE(priv->last_sync_nic_counter, nic_raw);

out:
	return msecs_to_jiffies(GVE_NIC_TS_SYNC_INTERVAL_MS);
}

static const struct ptp_clock_info gve_ptp_caps = {
	.owner          = THIS_MODULE,
	.name		= "gve clock",
	.gettimex64	= gve_ptp_gettimex64,
	.settime64	= gve_ptp_settime64,
	.do_aux_work	= gve_ptp_do_aux_work,
};

int gve_init_clock(struct gve_priv *priv)
{
	struct gve_ptp *ptp;
	u64 nic_raw;
	int err;

	ptp = kzalloc_obj(*priv->ptp);
	if (!ptp)
		return -ENOMEM;

	ptp->info = gve_ptp_caps;
	ptp->priv = priv;
	mutex_init(&ptp->nic_ts_read_lock);
	ptp->nic_ts_report =
		dma_alloc_coherent(&priv->pdev->dev,
				   sizeof(struct gve_nic_ts_report),
				   &ptp->nic_ts_report_bus, GFP_KERNEL);
	if (!ptp->nic_ts_report) {
		dev_err(&priv->pdev->dev, "%s dma alloc error\n", __func__);
		err = -ENOMEM;
		goto free_ptp;
	}

	err = gve_clock_nic_ts_read(ptp, &nic_raw);
	if (err) {
		dev_err(&priv->pdev->dev, "failed to read NIC clock %d\n", err);
		goto free_dma_mem;
	}
	WRITE_ONCE(priv->last_sync_nic_counter, nic_raw);

	ptp->clock = ptp_clock_register(&ptp->info, &priv->pdev->dev);
	if (IS_ERR(ptp->clock)) {
		dev_err(&priv->pdev->dev, "PTP clock registration failed\n");
		err = PTR_ERR(ptp->clock);
		goto free_dma_mem;
	}

	priv->ptp = ptp;
	ptp_schedule_worker(ptp->clock,
			    msecs_to_jiffies(GVE_NIC_TS_SYNC_INTERVAL_MS));

	return 0;

free_dma_mem:
	dma_free_coherent(&priv->pdev->dev, sizeof(struct gve_nic_ts_report),
			  ptp->nic_ts_report, ptp->nic_ts_report_bus);
	ptp->nic_ts_report = NULL;
free_ptp:
	mutex_destroy(&ptp->nic_ts_read_lock);
	kfree(ptp);
	return err;
}

void gve_teardown_clock(struct gve_priv *priv)
{
	struct gve_ptp *ptp = priv->ptp;

	if (!ptp)
		return;

	priv->ptp = NULL;
	ptp_clock_unregister(ptp->clock);
	dma_free_coherent(&priv->pdev->dev, sizeof(struct gve_nic_ts_report),
			  ptp->nic_ts_report, ptp->nic_ts_report_bus);
	ptp->nic_ts_report = NULL;
	mutex_destroy(&ptp->nic_ts_read_lock);
	kfree(ptp);
}
