/*
 * Thunderbolt service API
 *
 * Copyright (C) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2017, Intel Corporation
 * Authors: Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef THUNDERBOLT_H_
#define THUNDERBOLT_H_

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>

enum tb_cfg_pkg_type {
	TB_CFG_PKG_READ = 1,
	TB_CFG_PKG_WRITE = 2,
	TB_CFG_PKG_ERROR = 3,
	TB_CFG_PKG_NOTIFY_ACK = 4,
	TB_CFG_PKG_EVENT = 5,
	TB_CFG_PKG_XDOMAIN_REQ = 6,
	TB_CFG_PKG_XDOMAIN_RESP = 7,
	TB_CFG_PKG_OVERRIDE = 8,
	TB_CFG_PKG_RESET = 9,
	TB_CFG_PKG_ICM_EVENT = 10,
	TB_CFG_PKG_ICM_CMD = 11,
	TB_CFG_PKG_ICM_RESP = 12,
	TB_CFG_PKG_PREPARE_TO_SLEEP = 13,
};

/**
 * enum tb_security_level - Thunderbolt security level
 * @TB_SECURITY_NONE: No security, legacy mode
 * @TB_SECURITY_USER: User approval required at minimum
 * @TB_SECURITY_SECURE: One time saved key required at minimum
 * @TB_SECURITY_DPONLY: Only tunnel Display port (and USB)
 */
enum tb_security_level {
	TB_SECURITY_NONE,
	TB_SECURITY_USER,
	TB_SECURITY_SECURE,
	TB_SECURITY_DPONLY,
};

/**
 * struct tb - main thunderbolt bus structure
 * @dev: Domain device
 * @lock: Big lock. Must be held when accessing any struct
 *	  tb_switch / struct tb_port.
 * @nhi: Pointer to the NHI structure
 * @ctl: Control channel for this domain
 * @wq: Ordered workqueue for all domain specific work
 * @root_switch: Root switch of this domain
 * @cm_ops: Connection manager specific operations vector
 * @index: Linux assigned domain number
 * @security_level: Current security level
 * @privdata: Private connection manager specific data
 */
struct tb {
	struct device dev;
	struct mutex lock;
	struct tb_nhi *nhi;
	struct tb_ctl *ctl;
	struct workqueue_struct *wq;
	struct tb_switch *root_switch;
	const struct tb_cm_ops *cm_ops;
	int index;
	enum tb_security_level security_level;
	void *suse_kabi_padding;
	unsigned long privdata[0];
};

extern struct bus_type tb_bus_type;

#define TB_LINKS_PER_PHY_PORT	2

static inline unsigned int tb_phy_port_from_link(unsigned int link)
{
	return (link - 1) / TB_LINKS_PER_PHY_PORT;
}

/**
 * struct tb_property_dir - XDomain property directory
 * @uuid: Directory UUID or %NULL if root directory
 * @properties: List of properties in this directory
 *
 * User needs to provide serialization if needed.
 */
struct tb_property_dir {
	const uuid_t *uuid;
	struct list_head properties;
	void *suse_kabi_padding;
};

enum tb_property_type {
	TB_PROPERTY_TYPE_UNKNOWN = 0x00,
	TB_PROPERTY_TYPE_DIRECTORY = 0x44,
	TB_PROPERTY_TYPE_DATA = 0x64,
	TB_PROPERTY_TYPE_TEXT = 0x74,
	TB_PROPERTY_TYPE_VALUE = 0x76,
};

#define TB_PROPERTY_KEY_SIZE	8

/**
 * struct tb_property - XDomain property
 * @list: Used to link properties together in a directory
 * @key: Key for the property (always terminated).
 * @type: Type of the property
 * @length: Length of the property data in dwords
 * @value: Property value
 *
 * Users use @type to determine which field in @value is filled.
 */
struct tb_property {
	struct list_head list;
	char key[TB_PROPERTY_KEY_SIZE + 1];
	enum tb_property_type type;
	size_t length;
	union {
		struct tb_property_dir *dir;
		u8 *data;
		char *text;
		u32 immediate;
	} value;
	void *suse_kabi_padding;
};

struct tb_property_dir *tb_property_parse_dir(const u32 *block,
					      size_t block_len);
ssize_t tb_property_format_dir(const struct tb_property_dir *dir, u32 *block,
			       size_t block_len);
struct tb_property_dir *tb_property_create_dir(const uuid_t *uuid);
void tb_property_free_dir(struct tb_property_dir *dir);
int tb_property_add_immediate(struct tb_property_dir *parent, const char *key,
			      u32 value);
int tb_property_add_data(struct tb_property_dir *parent, const char *key,
			 const void *buf, size_t buflen);
int tb_property_add_text(struct tb_property_dir *parent, const char *key,
			 const char *text);
int tb_property_add_dir(struct tb_property_dir *parent, const char *key,
			struct tb_property_dir *dir);
void tb_property_remove(struct tb_property *tb_property);
struct tb_property *tb_property_find(struct tb_property_dir *dir,
			const char *key, enum tb_property_type type);
struct tb_property *tb_property_get_next(struct tb_property_dir *dir,
					 struct tb_property *prev);

#define tb_property_for_each(dir, property)			\
	for (property = tb_property_get_next(dir, NULL);	\
	     property;						\
	     property = tb_property_get_next(dir, property))

/**
 * struct tb_nhi - thunderbolt native host interface
 * @lock: Must be held during ring creation/destruction. Is acquired by
 *	  interrupt_work when dispatching interrupts to individual rings.
 * @pdev: Pointer to the PCI device
 * @iobase: MMIO space of the NHI
 * @tx_rings: All Tx rings available on this host controller
 * @rx_rings: All Rx rings available on this host controller
 * @msix_ida: Used to allocate MSI-X vectors for rings
 * @going_away: The host controller device is about to disappear so when
 *		this flag is set, avoid touching the hardware anymore.
 * @interrupt_work: Work scheduled to handle ring interrupt when no
 *		    MSI-X is used.
 * @hop_count: Number of rings (end point hops) supported by NHI.
 */
struct tb_nhi {
	spinlock_t lock;
	struct pci_dev *pdev;
	void __iomem *iobase;
	struct tb_ring **tx_rings;
	struct tb_ring **rx_rings;
	struct ida msix_ida;
	bool going_away;
	struct work_struct interrupt_work;
	u32 hop_count;
	void *suse_kabi_padding;
};

/**
 * struct tb_ring - thunderbolt TX or RX ring associated with a NHI
 * @lock: Lock serializing actions to this ring. Must be acquired after
 *	  nhi->lock.
 * @nhi: Pointer to the native host controller interface
 * @size: Size of the ring
 * @hop: Hop (DMA channel) associated with this ring
 * @head: Head of the ring (write next descriptor here)
 * @tail: Tail of the ring (complete next descriptor here)
 * @descriptors: Allocated descriptors for this ring
 * @queue: Queue holding frames to be transferred over this ring
 * @in_flight: Queue holding frames that are currently in flight
 * @work: Interrupt work structure
 * @is_tx: Is the ring Tx or Rx
 * @running: Is the ring running
 * @irq: MSI-X irq number if the ring uses MSI-X. %0 otherwise.
 * @vector: MSI-X vector number the ring uses (only set if @irq is > 0)
 * @flags: Ring specific flags
 * @sof_mask: Bit mask used to detect start of frame PDF
 * @eof_mask: Bit mask used to detect end of frame PDF
 * @start_poll: Called when ring interrupt is triggered to start
 *		polling. Passing %NULL keeps the ring in interrupt mode.
 * @poll_data: Data passed to @start_poll
 */
struct tb_ring {
	spinlock_t lock;
	struct tb_nhi *nhi;
	int size;
	int hop;
	int head;
	int tail;
	struct ring_desc *descriptors;
	dma_addr_t descriptors_dma;
	struct list_head queue;
	struct list_head in_flight;
	struct work_struct work;
	bool is_tx:1;
	bool running:1;
	int irq;
	u8 vector;
	unsigned int flags;
	u16 sof_mask;
	u16 eof_mask;
	void (*start_poll)(void *data);
	void *poll_data;
	void *suse_kabi_padding;
};

/* Leave ring interrupt enabled on suspend */
#define RING_FLAG_NO_SUSPEND	BIT(0)
/* Configure the ring to be in frame mode */
#define RING_FLAG_FRAME		BIT(1)
/* Enable end-to-end flow control */
#define RING_FLAG_E2E		BIT(2)

struct ring_frame;
typedef void (*ring_cb)(struct tb_ring *, struct ring_frame *, bool canceled);

/**
 * enum ring_desc_flags - Flags for DMA ring descriptor
 * %RING_DESC_ISOCH: Enable isonchronous DMA (Tx only)
 * %RING_DESC_CRC_ERROR: In frame mode CRC check failed for the frame (Rx only)
 * %RING_DESC_COMPLETED: Descriptor completed (set by NHI)
 * %RING_DESC_POSTED: Always set this
 * %RING_DESC_BUFFER_OVERRUN: RX buffer overrun
 * %RING_DESC_INTERRUPT: Request an interrupt on completion
 */
enum ring_desc_flags {
	RING_DESC_ISOCH = 0x1,
	RING_DESC_CRC_ERROR = 0x1,
	RING_DESC_COMPLETED = 0x2,
	RING_DESC_POSTED = 0x4,
	RING_DESC_BUFFER_OVERRUN = 0x04,
	RING_DESC_INTERRUPT = 0x8,
};

/**
 * struct ring_frame - For use with ring_rx/ring_tx
 * @buffer_phy: DMA mapped address of the frame
 * @callback: Callback called when the frame is finished (optional)
 * @list: Frame is linked to a queue using this
 * @size: Size of the frame in bytes (%0 means %4096)
 * @flags: Flags for the frame (see &enum ring_desc_flags)
 * @eof: End of frame protocol defined field
 * @sof: Start of frame protocol defined field
 */
struct ring_frame {
	dma_addr_t buffer_phy;
	ring_cb callback;
	struct list_head list;
	u32 size:12;
	u32 flags:12;
	u32 eof:4;
	u32 sof:4;
};

/* Minimum size for ring_rx */
#define TB_FRAME_SIZE		0x100

struct tb_ring *tb_ring_alloc_tx(struct tb_nhi *nhi, int hop, int size,
				 unsigned int flags);
struct tb_ring *tb_ring_alloc_rx(struct tb_nhi *nhi, int hop, int size,
				 unsigned int flags, u16 sof_mask, u16 eof_mask,
				 void (*start_poll)(void *), void *poll_data);
void tb_ring_start(struct tb_ring *ring);
void tb_ring_stop(struct tb_ring *ring);
void tb_ring_free(struct tb_ring *ring);

int __tb_ring_enqueue(struct tb_ring *ring, struct ring_frame *frame);

/**
 * tb_ring_rx() - enqueue a frame on an RX ring
 * @ring: Ring to enqueue the frame
 * @frame: Frame to enqueue
 *
 * @frame->buffer, @frame->buffer_phy have to be set. The buffer must
 * contain at least %TB_FRAME_SIZE bytes.
 *
 * @frame->callback will be invoked with @frame->size, @frame->flags,
 * @frame->eof, @frame->sof set once the frame has been received.
 *
 * If ring_stop() is called after the packet has been enqueued
 * @frame->callback will be called with canceled set to true.
 *
 * Return: Returns %-ESHUTDOWN if ring_stop has been called. Zero otherwise.
 */
static inline int tb_ring_rx(struct tb_ring *ring, struct ring_frame *frame)
{
	WARN_ON(ring->is_tx);
	return __tb_ring_enqueue(ring, frame);
}

/**
 * tb_ring_tx() - enqueue a frame on an TX ring
 * @ring: Ring the enqueue the frame
 * @frame: Frame to enqueue
 *
 * @frame->buffer, @frame->buffer_phy, @frame->size, @frame->eof and
 * @frame->sof have to be set.
 *
 * @frame->callback will be invoked with once the frame has been transmitted.
 *
 * If ring_stop() is called after the packet has been enqueued @frame->callback
 * will be called with canceled set to true.
 *
 * Return: Returns %-ESHUTDOWN if ring_stop has been called. Zero otherwise.
 */
static inline int tb_ring_tx(struct tb_ring *ring, struct ring_frame *frame)
{
	WARN_ON(!ring->is_tx);
	return __tb_ring_enqueue(ring, frame);
}

/* Used only when the ring is in polling mode */
struct ring_frame *tb_ring_poll(struct tb_ring *ring);
void tb_ring_poll_complete(struct tb_ring *ring);

#endif /* THUNDERBOLT_H_ */
