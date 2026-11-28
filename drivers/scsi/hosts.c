// SPDX-License-Identifier: GPL-2.0-only
/*
 *  hosts.c Copyright (C) 1992 Drew Eckhardt
 *          Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *          Copyright (C) 2002-2003 Christoph Hellwig
 *
 *  mid to lowlevel SCSI driver interface
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *  Added QLOGIC QLA1280 SCSI controller kernel host support. 
 *     August 4, 1999 Fred Lewis, Intel DuPont
 *
 *  Updated to reflect the new initialization scheme for the higher 
 *  level of scsi drivers (sd/sr/st)
 *  September 17, 2000 Torben Mathiasen <tmm@image.dk>
 *
 *  Restructured scsi_host lists and associated functions.
 *  September 04, 2002 Mike Anderson (andmike@us.ibm.com)
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/transport_class.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/idr.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_cmnd.h>
#include <linux/build_bug.h>

#include "scsi_priv.h"
#include "scsi_logging.h"


static int shost_eh_deadline = -1;

module_param_named(eh_deadline, shost_eh_deadline, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(eh_deadline,
		 "SCSI EH timeout in seconds (should be between 0 and 2^31-1)");

static DEFINE_IDA(host_index_ida);


static void scsi_host_cls_release(struct device *dev)
{
	put_device(&class_to_shost(dev)->shost_gendev);
}

static struct class shost_class = {
	.name		= "scsi_host",
	.dev_release	= scsi_host_cls_release,
	.dev_groups	= scsi_shost_groups,
};

/**
 *	scsi_host_set_state - Take the given host through the host state model.
 *	@shost:	scsi host to change the state of.
 *	@state:	state to change to.
 *
 *	Returns zero if unsuccessful or an error if the requested
 *	transition is illegal.
 **/
int scsi_host_set_state(struct Scsi_Host *shost, enum scsi_host_state state)
{
	enum scsi_host_state oldstate = shost->shost_state;

	if (state == oldstate)
		return 0;

	switch (state) {
	case SHOST_CREATED:
		/* There are no legal states that come back to
		 * created.  This is the manually initialised start
		 * state */
		goto illegal;

	case SHOST_RUNNING:
		switch (oldstate) {
		case SHOST_CREATED:
		case SHOST_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_RECOVERY:
		switch (oldstate) {
		case SHOST_RUNNING:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_CANCEL:
		switch (oldstate) {
		case SHOST_CREATED:
		case SHOST_RUNNING:
		case SHOST_CANCEL_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_DEL:
		switch (oldstate) {
		case SHOST_CANCEL:
		case SHOST_DEL_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_CANCEL_RECOVERY:
		switch (oldstate) {
		case SHOST_CANCEL:
		case SHOST_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_DEL_RECOVERY:
		switch (oldstate) {
		case SHOST_CANCEL_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;
	}
	shost->shost_state = state;
	return 0;

 illegal:
	SCSI_LOG_ERROR_RECOVERY(1,
				shost_printk(KERN_ERR, shost,
					     "Illegal host state transition"
					     "%s->%s\n",
					     scsi_host_state_name(oldstate),
					     scsi_host_state_name(state)));
	return -EINVAL;
}

/**
 * scsi_remove_host - remove a scsi host
 * @shost:	a pointer to a scsi host to remove
 **/
void scsi_remove_host(struct Scsi_Host *shost)
{
	unsigned long flags;

	mutex_lock(&shost->scan_mutex);
	spin_lock_irqsave(shost->host_lock, flags);
	if (scsi_host_set_state(shost, SHOST_CANCEL))
		if (scsi_host_set_state(shost, SHOST_CANCEL_RECOVERY)) {
			spin_unlock_irqrestore(shost->host_lock, flags);
			mutex_unlock(&shost->scan_mutex);
			return;
		}
	spin_unlock_irqrestore(shost->host_lock, flags);

	scsi_autopm_get_host(shost);
	flush_workqueue(shost->tmf_work_q);
	scsi_forget_host(shost);
	mutex_unlock(&shost->scan_mutex);
	scsi_proc_host_rm(shost);
	scsi_proc_hostdir_rm(shost->hostt);

	/*
	 * New SCSI devices cannot be attached anymore because of the SCSI host
	 * state so drop the tag set refcnt. Wait until the tag set refcnt drops
	 * to zero because .exit_cmd_priv implementations may need the host
	 * pointer.
	 */
	kref_put(&shost->tagset_refcnt, scsi_mq_free_tags);
	wait_for_completion(&shost->tagset_freed);

	spin_lock_irqsave(shost->host_lock, flags);
	if (scsi_host_set_state(shost, SHOST_DEL))
		BUG_ON(scsi_host_set_state(shost, SHOST_DEL_RECOVERY));
	spin_unlock_irqrestore(shost->host_lock, flags);

	transport_unregister_device(&shost->shost_gendev);
	device_unregister(&shost->shost_dev);
	device_del(&shost->shost_gendev);
}
EXPORT_SYMBOL(scsi_remove_host);

/**
 * scsi_add_host_with_dma - add a scsi host with dma device
 * @shost:	scsi host pointer to add
 * @dev:	a struct device of type scsi class
 * @dma_dev:	dma device for the host
 *
 * Note: You rarely need to worry about this unless you're in a
 * virtualised host environments, so use the simpler scsi_add_host()
 * function instead.
 *
 * Return value: 
 * 	0 on success / != 0 for error
 **/
int scsi_add_host_with_dma(struct Scsi_Host *shost, struct device *dev,
			   struct device *dma_dev)
{
	const struct scsi_host_template *sht = shost->hostt;
	int error = -EINVAL;

	shost_printk(KERN_INFO, shost, "%s\n",
			sht->info ? sht->info(shost) : sht->name);

	if (!shost->can_queue) {
		shost_printk(KERN_ERR, shost,
			     "can_queue = 0 no longer supported\n");
		goto fail;
	}

	/* Use min_t(int, ...) in case shost->can_queue exceeds SHRT_MAX */
	shost->cmd_per_lun = min_t(int, shost->cmd_per_lun,
				   shost->can_queue);

	error = scsi_init_sense_cache(shost);
	if (error)
		goto fail;

	if (!shost->shost_gendev.parent)
		shost->shost_gendev.parent = dev ? dev : &platform_bus;
	if (!dma_dev)
		dma_dev = shost->shost_gendev.parent;

	shost->dma_dev = dma_dev;

	if (dma_dev->dma_mask) {
		shost->max_sectors = min_t(unsigned int, shost->max_sectors,
				dma_max_mapping_size(dma_dev) >> SECTOR_SHIFT);
	}

	error = scsi_mq_setup_tags(shost);
	if (error)
		goto fail;

	kref_init(&shost->tagset_refcnt);
	init_completion(&shost->tagset_freed);

	/*
	 * Increase usage count temporarily here so that calling
	 * scsi_autopm_put_host() will trigger runtime idle if there is
	 * nothing else preventing suspending the device.
	 */
	pm_runtime_get_noresume(&shost->shost_gendev);
	pm_runtime_set_active(&shost->shost_gendev);
	pm_runtime_enable(&shost->shost_gendev);
	device_enable_async_suspend(&shost->shost_gendev);

	error = device_add(&shost->shost_gendev);
	if (error)
		goto out_disable_runtime_pm;

	scsi_host_set_state(shost, SHOST_RUNNING);
	get_device(shost->shost_gendev.parent);

	device_enable_async_suspend(&shost->shost_dev);

	get_device(&shost->shost_gendev);
	error = device_add(&shost->shost_dev);
	if (error)
		goto out_del_gendev;

	if (shost->transportt->host_size) {
		shost->shost_data = kzalloc(shost->transportt->host_size,
					 GFP_KERNEL);
		if (shost->shost_data == NULL) {
			error = -ENOMEM;
			goto out_del_dev;
		}
	}

	if (shost->transportt->create_work_queue) {
		snprintf(shost->work_q_name, sizeof(shost->work_q_name),
			 "scsi_wq_%d", shost->host_no);
		shost->work_q = alloc_workqueue("%s",
			WQ_SYSFS | __WQ_LEGACY | WQ_MEM_RECLAIM | WQ_UNBOUND,
			1, shost->work_q_name);

		if (!shost->work_q) {
			error = -EINVAL;
			goto out_del_dev;
		}
	}

	error = scsi_sysfs_add_host(shost);
	if (error)
		goto out_del_dev;

	scsi_proc_host_add(shost);
	scsi_autopm_put_host(shost);
	return error;

	/*
	 * Any host allocation in this function will be freed in
	 * scsi_host_dev_release().
	 */
 out_del_dev:
	device_del(&shost->shost_dev);
 out_del_gendev:
	/*
	 * Host state is SHOST_RUNNING so we have to explicitly release
	 * ->shost_dev.
	 */
	put_device(&shost->shost_dev);
	device_del(&shost->shost_gendev);
 out_disable_runtime_pm:
	device_disable_async_suspend(&shost->shost_gendev);
	pm_runtime_disable(&shost->shost_gendev);
	pm_runtime_set_suspended(&shost->shost_gendev);
	pm_runtime_put_noidle(&shost->shost_gendev);
	kref_put(&shost->tagset_refcnt, scsi_mq_free_tags);
 fail:
	return error;
}
EXPORT_SYMBOL(scsi_add_host_with_dma);

static void scsi_host_dev_release(struct device *dev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);
	struct device *parent = dev->parent;
	struct shost_work_struct *sws = shost_eh_work(shost);

	/* Wait for functions invoked through call_rcu(&scmd->rcu, ...) */
	rcu_barrier();

	cancel_work_sync(&sws->work);
	if (shost->tmf_work_q)
		destroy_workqueue(shost->tmf_work_q);
	if (shost->ehandler)
		kthread_stop(shost->ehandler);
	if (shost->work_q)
		destroy_workqueue(shost->work_q);

	if (shost->shost_state == SHOST_CREATED) {
		/*
		 * Free the shost_dev device name and remove the proc host dir
		 * here if scsi_host_{alloc,put}() have been called but neither
		 * scsi_host_add() nor scsi_remove_host() has been called.
		 * This avoids that the memory allocated for the shost_dev
		 * name as well as the proc dir structure are leaked.
		 */
		scsi_proc_hostdir_rm(shost->hostt);
		kfree(dev_name(&shost->shost_dev));
	}

	kfree(sws);
	kfree(shost->shost_data);

	ida_free(&host_index_ida, shost->host_no);

	if (shost->shost_state != SHOST_CREATED)
		put_device(parent);
	kfree(shost);
}

static struct device_type scsi_host_type = {
	.name =		"scsi_host",
	.release =	scsi_host_dev_release,
};

/**
 * scsi_host_alloc - register a scsi host adapter instance.
 * @sht:	pointer to scsi host template
 * @privsize:	extra bytes to allocate for driver
 *
 * Note:
 * 	Allocate a new Scsi_Host and perform basic initialization.
 * 	The host is not published to the scsi midlayer until scsi_add_host
 * 	is called.
 *
 * Return value:
 * 	Pointer to a new Scsi_Host
 **/
struct Scsi_Host *scsi_host_alloc(const struct scsi_host_template *sht, int privsize)
{
	struct Scsi_Host *shost;
	struct shost_work_struct *sws;
	int index;

	shost = kzalloc(sizeof(struct Scsi_Host) + privsize, GFP_KERNEL);
	if (!shost)
		return NULL;

	sws = kzalloc(sizeof(*sws), GFP_KERNEL);
	if (!sws) {
		kfree(shost);
		return NULL;
	}

	shost->host_lock = &shost->default_lock;
	spin_lock_init(shost->host_lock);
	shost->shost_state = SHOST_CREATED;
	INIT_LIST_HEAD(&shost->__devices);
	INIT_LIST_HEAD(&shost->__targets);
	INIT_LIST_HEAD(&shost->eh_abort_list);
	INIT_LIST_HEAD(&shost->eh_cmd_q);
	INIT_LIST_HEAD(&shost->starved_list);
	init_waitqueue_head(&shost->host_wait);
	mutex_init(&shost->scan_mutex);

	INIT_WORK(&sws->work, scsi_rcu_eh_wakeup);
	sws->shost = shost;
	shost->eh_work_high = upper_32_bits((unsigned long)sws);
	shost->eh_work_low = lower_32_bits((unsigned long)sws);

	index = ida_alloc(&host_index_ida, GFP_KERNEL);
	if (index < 0) {
		kfree(sws);
		kfree(shost);
		return NULL;
	}
	shost->host_no = index;

	shost->dma_channel = 0xff;

	/* These three are default values which can be overridden */
	shost->max_channel = 0;
	shost->max_id = 8;
	shost->max_lun = 8;

	/* Give each shost a default transportt */
	shost->transportt = &blank_transport_template;

	/*
	 * All drivers right now should be able to handle 12 byte
	 * commands.  Every so often there are requests for 16 byte
	 * commands, but individual low-level drivers need to certify that
	 * they actually do something sensible with such commands.
	 */
	shost->max_cmd_len = 12;
	shost->hostt = sht;
	shost->this_id = sht->this_id;
	shost->can_queue = sht->can_queue;
	shost->sg_tablesize = sht->sg_tablesize;
	shost->sg_prot_tablesize = sht->sg_prot_tablesize;
	shost->cmd_per_lun = sht->cmd_per_lun;
	shost->no_write_same = sht->no_write_same;
	shost->host_tagset = sht->host_tagset;
	shost->queuecommand_may_block = sht->queuecommand_may_block;

	if (shost_eh_deadline == -1 || !sht->eh_host_reset_handler)
		shost->eh_deadline = -1;
	else if ((ulong) shost_eh_deadline * HZ > INT_MAX) {
		shost_printk(KERN_WARNING, shost,
			     "eh_deadline %u too large, setting to %u\n",
			     shost_eh_deadline, INT_MAX / HZ);
		shost->eh_deadline = INT_MAX;
	} else
		shost->eh_deadline = shost_eh_deadline * HZ;

	if (sht->supported_mode == MODE_UNKNOWN)
		/* means we didn't set it ... default to INITIATOR */
		shost->active_mode = MODE_INITIATOR;
	else
		shost->active_mode = sht->supported_mode;

	if (sht->max_host_blocked)
		shost->max_host_blocked = sht->max_host_blocked;
	else
		shost->max_host_blocked = SCSI_DEFAULT_HOST_BLOCKED;

	/*
	 * If the driver imposes no hard sector transfer limit, start at
	 * machine infinity initially.
	 */
	if (sht->max_sectors)
		shost->max_sectors = sht->max_sectors;
	else
		shost->max_sectors = SCSI_DEFAULT_MAX_SECTORS;

	shost->virt_boundary_mask = sht->virt_boundary_mask;
	if (shost->virt_boundary_mask) {
		WARN_ON_ONCE(sht->max_segment_size &&
			     sht->max_segment_size != UINT_MAX);
		shost->max_segment_size = UINT_MAX;
	} else {
		if (sht->max_segment_size)
			shost->max_segment_size = sht->max_segment_size;
		else
			shost->max_segment_size = BLK_MAX_SEGMENT_SIZE;
	}

	/*
	 * assume a 4GB boundary, if not set
	 */
	if (sht->dma_boundary)
		shost->dma_boundary = sht->dma_boundary;
	else
		shost->dma_boundary = 0xffffffff;

	device_initialize(&shost->shost_gendev);
	dev_set_name(&shost->shost_gendev, "host%d", shost->host_no);
	shost->shost_gendev.bus = &scsi_bus_type;
	shost->shost_gendev.type = &scsi_host_type;
	scsi_enable_async_suspend(&shost->shost_gendev);

	device_initialize(&shost->shost_dev);
	shost->shost_dev.parent = &shost->shost_gendev;
	shost->shost_dev.class = &shost_class;
	dev_set_name(&shost->shost_dev, "host%d", shost->host_no);
	shost->shost_dev.groups = sht->shost_groups;

	shost->ehandler = kthread_run(scsi_error_handler, shost,
			"scsi_eh_%d", shost->host_no);
	if (IS_ERR(shost->ehandler)) {
		shost_printk(KERN_WARNING, shost,
			"error handler thread failed to spawn, error = %ld\n",
			PTR_ERR(shost->ehandler));
		shost->ehandler = NULL;
		goto fail;
	}

	shost->tmf_work_q = alloc_workqueue("scsi_tmf_%d",
					WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_SYSFS,
					   1, shost->host_no);
	if (!shost->tmf_work_q) {
		shost_printk(KERN_WARNING, shost,
			     "failed to create tmf workq\n");
		goto fail;
	}
	if (scsi_proc_hostdir_add(shost->hostt) < 0)
		goto fail;
	return shost;
 fail:
	/*
	 * Host state is still SHOST_CREATED and that is enough to release
	 * ->shost_gendev. scsi_host_dev_release() will free
	 * dev_name(&shost->shost_dev).
	 */
	put_device(&shost->shost_gendev);

	return NULL;
}
EXPORT_SYMBOL(scsi_host_alloc);

static int __scsi_host_match(struct device *dev, const void *data)
{
	struct Scsi_Host *p;
	const unsigned int *hostnum = data;

	p = class_to_shost(dev);
	return p->host_no == *hostnum;
}

/**
 * scsi_host_lookup - get a reference to a Scsi_Host by host no
 * @hostnum:	host number to locate
 *
 * Return value:
 *	A pointer to located Scsi_Host or NULL.
 *
 *	The caller must do a scsi_host_put() to drop the reference
 *	that scsi_host_get() took. The put_device() below dropped
 *	the reference from class_find_device().
 **/
struct Scsi_Host *scsi_host_lookup(unsigned int hostnum)
{
	struct device *cdev;
	struct Scsi_Host *shost = NULL;

	cdev = class_find_device(&shost_class, NULL, &hostnum,
				 __scsi_host_match);
	if (cdev) {
		shost = scsi_host_get(class_to_shost(cdev));
		put_device(cdev);
	}
	return shost;
}
EXPORT_SYMBOL(scsi_host_lookup);

/**
 * scsi_host_get - inc a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to inc.
 **/
struct Scsi_Host *scsi_host_get(struct Scsi_Host *shost)
{
	if ((shost->shost_state == SHOST_DEL) ||
		!get_device(&shost->shost_gendev))
		return NULL;
	return shost;
}
EXPORT_SYMBOL(scsi_host_get);

static bool scsi_host_check_in_flight(struct request *rq, void *data)
{
	int *count = data;
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);

	if (test_bit(SCMD_STATE_INFLIGHT, &cmd->state))
		(*count)++;

	return true;
}

/**
 * scsi_host_busy - Return the host busy counter
 * @shost:	Pointer to Scsi_Host to inc.
 **/
int scsi_host_busy(struct Scsi_Host *shost)
{
	int cnt = 0;

	blk_mq_tagset_busy_iter(&shost->tag_set,
				scsi_host_check_in_flight, &cnt);
	return cnt;
}
EXPORT_SYMBOL(scsi_host_busy);

/**
 * scsi_host_put - dec a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to dec.
 **/
void scsi_host_put(struct Scsi_Host *shost)
{
	put_device(&shost->shost_gendev);
}
EXPORT_SYMBOL(scsi_host_put);

int scsi_init_hosts(void)
{
	return class_register(&shost_class);
}

void scsi_exit_hosts(void)
{
	class_unregister(&shost_class);
	ida_destroy(&host_index_ida);
}

int scsi_is_host_device(const struct device *dev)
{
	return dev->type == &scsi_host_type;
}
EXPORT_SYMBOL(scsi_is_host_device);

/**
 * scsi_queue_work - Queue work to the Scsi_Host workqueue.
 * @shost:	Pointer to Scsi_Host.
 * @work:	Work to queue for execution.
 *
 * Return value:
 * 	1 - work queued for execution
 *	0 - work is already queued
 *	-EINVAL - work queue doesn't exist
 **/
int scsi_queue_work(struct Scsi_Host *shost, struct work_struct *work)
{
	if (unlikely(!shost->work_q)) {
		shost_printk(KERN_ERR, shost,
			"ERROR: Scsi host '%s' attempted to queue scsi-work, "
			"when no workqueue created.\n", shost->hostt->name);
		dump_stack();

		return -EINVAL;
	}

	return queue_work(shost->work_q, work);
}
EXPORT_SYMBOL_GPL(scsi_queue_work);

/**
 * scsi_flush_work - Flush a Scsi_Host's workqueue.
 * @shost:	Pointer to Scsi_Host.
 **/
void scsi_flush_work(struct Scsi_Host *shost)
{
	if (!shost->work_q) {
		shost_printk(KERN_ERR, shost,
			"ERROR: Scsi host '%s' attempted to flush scsi-work, "
			"when no workqueue created.\n", shost->hostt->name);
		dump_stack();
		return;
	}

	flush_workqueue(shost->work_q);
}
EXPORT_SYMBOL_GPL(scsi_flush_work);

static bool complete_all_cmds_iter(struct request *rq, void *data)
{
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);
	enum scsi_host_status status = *(enum scsi_host_status *)data;

	scsi_dma_unmap(scmd);
	scmd->result = 0;
	set_host_byte(scmd, status);
	scsi_done(scmd);
	return true;
}

/**
 * scsi_host_complete_all_commands - Terminate all running commands
 * @shost:	Scsi Host on which commands should be terminated
 * @status:	Status to be set for the terminated commands
 *
 * There is no protection against modification of the number
 * of outstanding commands. It is the responsibility of the
 * caller to ensure that concurrent I/O submission and/or
 * completion is stopped when calling this function.
 */
void scsi_host_complete_all_commands(struct Scsi_Host *shost,
				     enum scsi_host_status status)
{
	blk_mq_tagset_busy_iter(&shost->tag_set, complete_all_cmds_iter,
				&status);
}
EXPORT_SYMBOL_GPL(scsi_host_complete_all_commands);

struct scsi_host_busy_iter_data {
	bool (*fn)(struct scsi_cmnd *, void *);
	void *priv;
};

static bool __scsi_host_busy_iter_fn(struct request *req, void *priv)
{
	struct scsi_host_busy_iter_data *iter_data = priv;
	struct scsi_cmnd *sc = blk_mq_rq_to_pdu(req);

	return iter_data->fn(sc, iter_data->priv);
}

/**
 * scsi_host_busy_iter - Iterate over all busy commands
 * @shost:	Pointer to Scsi_Host.
 * @fn:		Function to call on each busy command
 * @priv:	Data pointer passed to @fn
 *
 * If locking against concurrent command completions is required
 * ithas to be provided by the caller
 **/
void scsi_host_busy_iter(struct Scsi_Host *shost,
			 bool (*fn)(struct scsi_cmnd *, void *),
			 void *priv)
{
	struct scsi_host_busy_iter_data iter_data = {
		.fn = fn,
		.priv = priv,
	};

	blk_mq_tagset_busy_iter(&shost->tag_set, __scsi_host_busy_iter_fn,
				&iter_data);
}
EXPORT_SYMBOL_GPL(scsi_host_busy_iter);

/* Static kABI check for the kABI fix for dccf3b1798b7 */
struct __orig_Scsi_Host {
	/*
	 * __devices is protected by the host_lock, but you should
	 * usually use scsi_device_lookup / shost_for_each_device
	 * to access it and don't care about locking yourself.
	 * In the rare case of being in irq context you can use
	 * their __ prefixed variants with the lock held. NEVER
	 * access this list directly from a driver.
	 */
	struct list_head	__devices;
	struct list_head	__targets;
	
	struct list_head	starved_list;

	spinlock_t		default_lock;
	spinlock_t		*host_lock;

	struct mutex		scan_mutex;/* serialize scanning activity */

	struct list_head	eh_abort_list;
	struct list_head	eh_cmd_q;
	struct task_struct    * ehandler;  /* Error recovery thread. */
	struct completion     * eh_action; /* Wait for specific actions on the
					      host. */
	wait_queue_head_t       host_wait;
	const struct scsi_host_template *hostt;
	struct scsi_transport_template *transportt;

	struct kref		tagset_refcnt;
	struct completion	tagset_freed;
	/* Area to keep a shared tag map */
	struct blk_mq_tag_set	tag_set;

	atomic_t host_blocked;

	unsigned int host_failed;	   /* commands that failed.
					      protected by host_lock */
	unsigned int host_eh_scheduled;    /* EH scheduled without command */
    
	unsigned int host_no;  /* Used for IOCTL_GET_IDLUN, /proc/scsi et al. */

	/* next two fields are used to bound the time spent in error handling */
	int eh_deadline;
	unsigned long last_reset;


	/*
	 * These three parameters can be used to allow for wide scsi,
	 * and for host adapters that support multiple busses
	 * The last two should be set to 1 more than the actual max id
	 * or lun (e.g. 8 for SCSI parallel systems).
	 */
	unsigned int max_channel;
	unsigned int max_id;
	u64 max_lun;

	/*
	 * This is a unique identifier that must be assigned so that we
	 * have some way of identifying each detected host adapter properly
	 * and uniquely.  For hosts that do not support more than one card
	 * in the system at one time, this does not need to be set.  It is
	 * initialized to 0 in scsi_register.
	 */
	unsigned int unique_id;

	/*
	 * The maximum length of SCSI commands that this host can accept.
	 * Probably 12 for most host adapters, but could be 16 for others.
	 * or 260 if the driver supports variable length cdbs.
	 * For drivers that don't set this field, a value of 12 is
	 * assumed.
	 */
	unsigned short max_cmd_len;

	int this_id;
	int can_queue;
	short cmd_per_lun;
	short unsigned int sg_tablesize;
	short unsigned int sg_prot_tablesize;
	unsigned int max_sectors;
	unsigned int opt_sectors;
	unsigned int max_segment_size;
	unsigned long dma_boundary;
	unsigned long virt_boundary_mask;
	/*
	 * In scsi-mq mode, the number of hardware queues supported by the LLD.
	 *
	 * Note: it is assumed that each hardware queue has a queue depth of
	 * can_queue. In other words, the total queue depth per host
	 * is nr_hw_queues * can_queue. However, for when host_tagset is set,
	 * the total queue depth is can_queue.
	 */
	unsigned nr_hw_queues;
	unsigned nr_maps;
	unsigned active_mode:2;

	/*
	 * Host has requested that no further requests come through for the
	 * time being.
	 */
	unsigned host_self_blocked:1;
    
	/*
	 * Host uses correct SCSI ordering not PC ordering. The bit is
	 * set for the minority of drivers whose authors actually read
	 * the spec ;).
	 */
	unsigned reverse_ordering:1;

	/* Task mgmt function in progress */
	unsigned tmf_in_progress:1;

	/* Asynchronous scan in progress */
	unsigned async_scan:1;

	/* Don't resume host in EH */
	unsigned eh_noresume:1;

	/* The controller does not support WRITE SAME */
	unsigned no_write_same:1;

	/* True if the host uses host-wide tagspace */
	unsigned host_tagset:1;

	/* The queuecommand callback may block. See also BLK_MQ_F_BLOCKING. */
	unsigned queuecommand_may_block:1;

	/* Host responded with short (<36 bytes) INQUIRY result */
	unsigned short_inquiry:1;

	/* The transport requires the LUN bits NOT to be stored in CDB[1] */
	unsigned no_scsi2_lun_in_cdb:1;

	/*
	 * Optional work queue to be utilized by the transport
	 */
	char work_q_name[20];
	struct workqueue_struct *work_q;

	/*
	 * Task management function work queue
	 */
	struct workqueue_struct *tmf_work_q;

	/*
	 * Value host_blocked counts down from
	 */
	unsigned int max_host_blocked;

	/* Protection Information */
	unsigned int prot_capabilities;
	unsigned char prot_guard_type;

	/* legacy crap */
	unsigned long base;
	unsigned long io_port;
	unsigned char n_io_port;
	unsigned char dma_channel;
	unsigned int  irq;
	

	enum scsi_host_state shost_state;

	/* ldm bits */
	struct device		shost_gendev, shost_dev;

	/*
	 * Points to the transport data (if any) which is allocated
	 * separately
	 */
	void *shost_data;

	/*
	 * Points to the physical bus device we'd use to do DMA
	 * Needed just in case we have virtual hosts.
	 */
	struct device *dma_dev;

	/*
	 * We should ensure that this is aligned, both for better performance
	 * and also because some compilers (m68k) don't automatically force
	 * alignment to a long boundary.
	 */
	unsigned long hostdata[]  /* Used for storage of host specific stuff */
		__attribute__ ((aligned (sizeof(unsigned long))));
};

suse_kabi_static_assert(offsetof(struct Scsi_Host, last_reset) ==
			offsetof(struct __orig_Scsi_Host, last_reset));
suse_kabi_static_assert(offsetof(struct Scsi_Host, tagset_freed) ==
			offsetof(struct __orig_Scsi_Host, tagset_freed));
suse_kabi_static_assert(offsetof(struct Scsi_Host, hostdata) ==
			offsetof(struct __orig_Scsi_Host, hostdata));
suse_kabi_static_assert(sizeof(struct Scsi_Host) == sizeof(struct __orig_Scsi_Host));
