// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015 Microchip Technology
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/phylink.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>
#include <linux/uaccess.h>
#include <linux/linkmode.h>
#include <linux/list.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <net/ip6_checksum.h>
#include <net/vxlan.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/microchipphy.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include "lan78xx.h"

#define DRIVER_AUTHOR	"WOOJUNG HUH <woojung.huh@microchip.com>"
#define DRIVER_DESC	"LAN78XX USB 3.0 Gigabit Ethernet Devices"
#define DRIVER_NAME	"lan78xx"

#define TX_TIMEOUT_JIFFIES		(5 * HZ)
#define THROTTLE_JIFFIES		(HZ / 8)
#define UNLINK_TIMEOUT_MS		3

#define RX_MAX_QUEUE_MEMORY		(60 * 1518)

#define SS_USB_PKT_SIZE			(1024)
#define HS_USB_PKT_SIZE			(512)
#define FS_USB_PKT_SIZE			(64)

#define MAX_RX_FIFO_SIZE		(12 * 1024)
#define MAX_TX_FIFO_SIZE		(12 * 1024)

#define FLOW_THRESHOLD(n)		((((n) + 511) / 512) & 0x7F)
#define FLOW_CTRL_THRESHOLD(on, off)	((FLOW_THRESHOLD(on)  << 0) | \
					 (FLOW_THRESHOLD(off) << 8))

/* Flow control turned on when Rx FIFO level rises above this level (bytes) */
#define FLOW_ON_SS			9216
#define FLOW_ON_HS			8704

/* Flow control turned off when Rx FIFO level falls below this level (bytes) */
#define FLOW_OFF_SS			4096
#define FLOW_OFF_HS			1024

#define DEFAULT_BURST_CAP_SIZE		(MAX_TX_FIFO_SIZE)
#define DEFAULT_BULK_IN_DELAY		(0x0800)
#define MAX_SINGLE_PACKET_SIZE		(9000)
#define DEFAULT_TX_CSUM_ENABLE		(true)
#define DEFAULT_RX_CSUM_ENABLE		(true)
#define DEFAULT_TSO_CSUM_ENABLE		(true)
#define DEFAULT_VLAN_FILTER_ENABLE	(true)
#define DEFAULT_VLAN_RX_OFFLOAD		(true)
#define TX_ALIGNMENT			(4)
#define RXW_PADDING			2

#define LAN78XX_USB_VENDOR_ID		(0x0424)
#define LAN7800_USB_PRODUCT_ID		(0x7800)
#define LAN7850_USB_PRODUCT_ID		(0x7850)
#define LAN7801_USB_PRODUCT_ID		(0x7801)
#define LAN78XX_EEPROM_MAGIC		(0x78A5)
#define LAN78XX_OTP_MAGIC		(0x78F3)
#define AT29M2AF_USB_VENDOR_ID		(0x07C9)
#define AT29M2AF_USB_PRODUCT_ID	(0x0012)

#define	MII_READ			1
#define	MII_WRITE			0

#define EEPROM_INDICATOR		(0xA5)
#define EEPROM_MAC_OFFSET		(0x01)
#define MAX_EEPROM_SIZE			512
#define OTP_INDICATOR_1			(0xF3)
#define OTP_INDICATOR_2			(0xF7)

#define WAKE_ALL			(WAKE_PHY | WAKE_UCAST | \
					 WAKE_MCAST | WAKE_BCAST | \
					 WAKE_ARP | WAKE_MAGIC)

#define TX_URB_NUM			10
#define TX_SS_URB_NUM			TX_URB_NUM
#define TX_HS_URB_NUM			TX_URB_NUM
#define TX_FS_URB_NUM			TX_URB_NUM

/* A single URB buffer must be large enough to hold a complete jumbo packet
 */
#define TX_SS_URB_SIZE			(32 * 1024)
#define TX_HS_URB_SIZE			(16 * 1024)
#define TX_FS_URB_SIZE			(10 * 1024)

#define RX_SS_URB_NUM			30
#define RX_HS_URB_NUM			10
#define RX_FS_URB_NUM			10
#define RX_SS_URB_SIZE			TX_SS_URB_SIZE
#define RX_HS_URB_SIZE			TX_HS_URB_SIZE
#define RX_FS_URB_SIZE			TX_FS_URB_SIZE

#define SS_BURST_CAP_SIZE		RX_SS_URB_SIZE
#define SS_BULK_IN_DELAY		0x2000
#define HS_BURST_CAP_SIZE		RX_HS_URB_SIZE
#define HS_BULK_IN_DELAY		0x2000
#define FS_BURST_CAP_SIZE		RX_FS_URB_SIZE
#define FS_BULK_IN_DELAY		0x2000

#define TX_CMD_LEN			8
#define TX_SKB_MIN_LEN			(TX_CMD_LEN + ETH_HLEN)
#define LAN78XX_TSO_SIZE(dev)		((dev)->tx_urb_size - TX_SKB_MIN_LEN)

#define RX_CMD_LEN			10
#define RX_SKB_MIN_LEN			(RX_CMD_LEN + ETH_HLEN)
#define RX_MAX_FRAME_LEN(mtu)		((mtu) + ETH_HLEN + VLAN_HLEN)

/* USB related defines */
#define BULK_IN_PIPE			1
#define BULK_OUT_PIPE			2

/* default autosuspend delay (mSec)*/
#define DEFAULT_AUTOSUSPEND_DELAY	(10 * 1000)

/* statistic update interval (mSec) */
#define STAT_UPDATE_TIMER		(1 * 1000)

/* time to wait for MAC or FCT to stop (jiffies) */
#define HW_DISABLE_TIMEOUT		(HZ / 10)

/* time to wait between polling MAC or FCT state (ms) */
#define HW_DISABLE_DELAY_MS		1

/* defines interrupts from interrupt EP */
#define MAX_INT_EP			(32)
#define INT_EP_INTEP			(31)
#define INT_EP_OTP_WR_DONE		(28)
#define INT_EP_EEE_TX_LPI_START		(26)
#define INT_EP_EEE_TX_LPI_STOP		(25)
#define INT_EP_EEE_RX_LPI		(24)
#define INT_EP_MAC_RESET_TIMEOUT	(23)
#define INT_EP_RDFO			(22)
#define INT_EP_TXE			(21)
#define INT_EP_USB_STATUS		(20)
#define INT_EP_TX_DIS			(19)
#define INT_EP_RX_DIS			(18)
#define INT_EP_PHY			(17)
#define INT_EP_DP			(16)
#define INT_EP_MAC_ERR			(15)
#define INT_EP_TDFU			(14)
#define INT_EP_TDFO			(13)
#define INT_EP_UTX			(12)
#define INT_EP_GPIO_11			(11)
#define INT_EP_GPIO_10			(10)
#define INT_EP_GPIO_9			(9)
#define INT_EP_GPIO_8			(8)
#define INT_EP_GPIO_7			(7)
#define INT_EP_GPIO_6			(6)
#define INT_EP_GPIO_5			(5)
#define INT_EP_GPIO_4			(4)
#define INT_EP_GPIO_3			(3)
#define INT_EP_GPIO_2			(2)
#define INT_EP_GPIO_1			(1)
#define INT_EP_GPIO_0			(0)

static const char lan78xx_gstrings[][ETH_GSTRING_LEN] = {
	"RX FCS Errors",
	"RX Alignment Errors",
	"Rx Fragment Errors",
	"RX Jabber Errors",
	"RX Undersize Frame Errors",
	"RX Oversize Frame Errors",
	"RX Dropped Frames",
	"RX Unicast Byte Count",
	"RX Broadcast Byte Count",
	"RX Multicast Byte Count",
	"RX Unicast Frames",
	"RX Broadcast Frames",
	"RX Multicast Frames",
	"RX Pause Frames",
	"RX 64 Byte Frames",
	"RX 65 - 127 Byte Frames",
	"RX 128 - 255 Byte Frames",
	"RX 256 - 511 Bytes Frames",
	"RX 512 - 1023 Byte Frames",
	"RX 1024 - 1518 Byte Frames",
	"RX Greater 1518 Byte Frames",
	"EEE RX LPI Transitions",
	"EEE RX LPI Time",
	"TX FCS Errors",
	"TX Excess Deferral Errors",
	"TX Carrier Errors",
	"TX Bad Byte Count",
	"TX Single Collisions",
	"TX Multiple Collisions",
	"TX Excessive Collision",
	"TX Late Collisions",
	"TX Unicast Byte Count",
	"TX Broadcast Byte Count",
	"TX Multicast Byte Count",
	"TX Unicast Frames",
	"TX Broadcast Frames",
	"TX Multicast Frames",
	"TX Pause Frames",
	"TX 64 Byte Frames",
	"TX 65 - 127 Byte Frames",
	"TX 128 - 255 Byte Frames",
	"TX 256 - 511 Bytes Frames",
	"TX 512 - 1023 Byte Frames",
	"TX 1024 - 1518 Byte Frames",
	"TX Greater 1518 Byte Frames",
	"EEE TX LPI Transitions",
	"EEE TX LPI Time",
};

struct lan78xx_statstage {
	u32 rx_fcs_errors;
	u32 rx_alignment_errors;
	u32 rx_fragment_errors;
	u32 rx_jabber_errors;
	u32 rx_undersize_frame_errors;
	u32 rx_oversize_frame_errors;
	u32 rx_dropped_frames;
	u32 rx_unicast_byte_count;
	u32 rx_broadcast_byte_count;
	u32 rx_multicast_byte_count;
	u32 rx_unicast_frames;
	u32 rx_broadcast_frames;
	u32 rx_multicast_frames;
	u32 rx_pause_frames;
	u32 rx_64_byte_frames;
	u32 rx_65_127_byte_frames;
	u32 rx_128_255_byte_frames;
	u32 rx_256_511_bytes_frames;
	u32 rx_512_1023_byte_frames;
	u32 rx_1024_1518_byte_frames;
	u32 rx_greater_1518_byte_frames;
	u32 eee_rx_lpi_transitions;
	u32 eee_rx_lpi_time;
	u32 tx_fcs_errors;
	u32 tx_excess_deferral_errors;
	u32 tx_carrier_errors;
	u32 tx_bad_byte_count;
	u32 tx_single_collisions;
	u32 tx_multiple_collisions;
	u32 tx_excessive_collision;
	u32 tx_late_collisions;
	u32 tx_unicast_byte_count;
	u32 tx_broadcast_byte_count;
	u32 tx_multicast_byte_count;
	u32 tx_unicast_frames;
	u32 tx_broadcast_frames;
	u32 tx_multicast_frames;
	u32 tx_pause_frames;
	u32 tx_64_byte_frames;
	u32 tx_65_127_byte_frames;
	u32 tx_128_255_byte_frames;
	u32 tx_256_511_bytes_frames;
	u32 tx_512_1023_byte_frames;
	u32 tx_1024_1518_byte_frames;
	u32 tx_greater_1518_byte_frames;
	u32 eee_tx_lpi_transitions;
	u32 eee_tx_lpi_time;
};

struct lan78xx_statstage64 {
	u64 rx_fcs_errors;
	u64 rx_alignment_errors;
	u64 rx_fragment_errors;
	u64 rx_jabber_errors;
	u64 rx_undersize_frame_errors;
	u64 rx_oversize_frame_errors;
	u64 rx_dropped_frames;
	u64 rx_unicast_byte_count;
	u64 rx_broadcast_byte_count;
	u64 rx_multicast_byte_count;
	u64 rx_unicast_frames;
	u64 rx_broadcast_frames;
	u64 rx_multicast_frames;
	u64 rx_pause_frames;
	u64 rx_64_byte_frames;
	u64 rx_65_127_byte_frames;
	u64 rx_128_255_byte_frames;
	u64 rx_256_511_bytes_frames;
	u64 rx_512_1023_byte_frames;
	u64 rx_1024_1518_byte_frames;
	u64 rx_greater_1518_byte_frames;
	u64 eee_rx_lpi_transitions;
	u64 eee_rx_lpi_time;
	u64 tx_fcs_errors;
	u64 tx_excess_deferral_errors;
	u64 tx_carrier_errors;
	u64 tx_bad_byte_count;
	u64 tx_single_collisions;
	u64 tx_multiple_collisions;
	u64 tx_excessive_collision;
	u64 tx_late_collisions;
	u64 tx_unicast_byte_count;
	u64 tx_broadcast_byte_count;
	u64 tx_multicast_byte_count;
	u64 tx_unicast_frames;
	u64 tx_broadcast_frames;
	u64 tx_multicast_frames;
	u64 tx_pause_frames;
	u64 tx_64_byte_frames;
	u64 tx_65_127_byte_frames;
	u64 tx_128_255_byte_frames;
	u64 tx_256_511_bytes_frames;
	u64 tx_512_1023_byte_frames;
	u64 tx_1024_1518_byte_frames;
	u64 tx_greater_1518_byte_frames;
	u64 eee_tx_lpi_transitions;
	u64 eee_tx_lpi_time;
};

static u32 lan78xx_regs[] = {
	ID_REV,
	INT_STS,
	HW_CFG,
	PMT_CTL,
	E2P_CMD,
	E2P_DATA,
	USB_STATUS,
	VLAN_TYPE,
	MAC_CR,
	MAC_RX,
	MAC_TX,
	FLOW,
	ERR_STS,
	MII_ACC,
	MII_DATA,
	EEE_TX_LPI_REQ_DLY,
	EEE_TW_TX_SYS,
	EEE_TX_LPI_REM_DLY,
	WUCSR
};

#define PHY_REG_SIZE (32 * sizeof(u32))

struct lan78xx_net;

struct lan78xx_priv {
	struct lan78xx_net *dev;
	u32 rfe_ctl;
	u32 mchash_table[DP_SEL_VHF_HASH_LEN]; /* multicast hash table */
	u32 pfilter_table[NUM_OF_MAF][2]; /* perfect filter table */
	u32 vlan_table[DP_SEL_VHF_VLAN_LEN];
	struct mutex dataport_mutex; /* for dataport access */
	spinlock_t rfe_ctl_lock; /* for rfe register access */
	struct work_struct set_multicast;
	struct work_struct set_vlan;
	u32 wol;
};

enum skb_state {
	illegal = 0,
	tx_start,
	tx_done,
	rx_start,
	rx_done,
	rx_cleanup,
	unlink_start
};

struct skb_data {		/* skb->cb is one of these */
	struct urb *urb;
	struct lan78xx_net *dev;
	enum skb_state state;
	size_t length;
	int num_of_packet;
};

#define EVENT_TX_HALT			0
#define EVENT_RX_HALT			1
#define EVENT_RX_MEMORY			2
#define EVENT_STS_SPLIT			3
#define EVENT_PHY_INT_ACK		4
#define EVENT_RX_PAUSED			5
#define EVENT_DEV_WAKING		6
#define EVENT_DEV_ASLEEP		7
#define EVENT_DEV_OPEN			8
#define EVENT_STAT_UPDATE		9
#define EVENT_DEV_DISCONNECT		10

struct statstage {
	struct mutex			access_lock;	/* for stats access */
	struct lan78xx_statstage	saved;
	struct lan78xx_statstage	rollover_count;
	struct lan78xx_statstage	rollover_max;
	struct lan78xx_statstage64	curr_stat;
};

struct irq_domain_data {
	struct irq_domain	*irqdomain;
	unsigned int		phyirq;
	struct irq_chip		*irqchip;
	irq_flow_handler_t	irq_handler;
	u32			irqenable;
	struct mutex		irq_lock;		/* for irq bus access */
};

struct lan78xx_net {
	struct net_device	*net;
	struct usb_device	*udev;
	struct usb_interface	*intf;

	unsigned int		tx_pend_data_len;
	size_t			n_tx_urbs;
	size_t			n_rx_urbs;
	size_t			tx_urb_size;
	size_t			rx_urb_size;

	struct sk_buff_head	rxq_free;
	struct sk_buff_head	rxq;
	struct sk_buff_head	rxq_done;
	struct sk_buff_head	rxq_overflow;
	struct sk_buff_head	txq_free;
	struct sk_buff_head	txq;
	struct sk_buff_head	txq_pend;

	struct napi_struct	napi;

	struct delayed_work	wq;

	int			msg_enable;

	struct urb		*urb_intr;
	struct usb_anchor	deferred;

	struct mutex		dev_mutex; /* serialise open/stop wrt suspend/resume */
	struct mutex		mdiobus_mutex; /* for MDIO bus access */
	unsigned int		pipe_in, pipe_out, pipe_intr;

	unsigned int		bulk_in_delay;
	unsigned int		burst_cap;

	unsigned long		flags;

	wait_queue_head_t	*wait;

	unsigned int		maxpacket;
	struct timer_list	stat_monitor;

	unsigned long		data[5];

	u32			chipid;
	u32			chiprev;
	struct mii_bus		*mdiobus;
	phy_interface_t		interface;

	int			delta;
	struct statstage	stats;

	struct irq_domain_data	domain_data;

	struct phylink		*phylink;
	struct phylink_config	phylink_config;
};

/* use ethtool to change the level for any given device */
static int msg_level = -1;
module_param(msg_level, int, 0);
MODULE_PARM_DESC(msg_level, "Override default message level");

static struct sk_buff *lan78xx_get_buf(struct sk_buff_head *buf_pool)
{
	if (skb_queue_empty(buf_pool))
		return NULL;

	return skb_dequeue(buf_pool);
}

static void lan78xx_release_buf(struct sk_buff_head *buf_pool,
				struct sk_buff *buf)
{
	buf->data = buf->head;
	skb_reset_tail_pointer(buf);

	buf->len = 0;
	buf->data_len = 0;

	skb_queue_tail(buf_pool, buf);
}

static void lan78xx_free_buf_pool(struct sk_buff_head *buf_pool)
{
	struct skb_data *entry;
	struct sk_buff *buf;

	while (!skb_queue_empty(buf_pool)) {
		buf = skb_dequeue(buf_pool);
		if (buf) {
			entry = (struct skb_data *)buf->cb;
			usb_free_urb(entry->urb);
			dev_kfree_skb_any(buf);
		}
	}
}

static int lan78xx_alloc_buf_pool(struct sk_buff_head *buf_pool,
				  size_t n_urbs, size_t urb_size,
				  struct lan78xx_net *dev)
{
	struct skb_data *entry;
	struct sk_buff *buf;
	struct urb *urb;
	int i;

	skb_queue_head_init(buf_pool);

	for (i = 0; i < n_urbs; i++) {
		buf = alloc_skb(urb_size, GFP_ATOMIC);
		if (!buf)
			goto error;

		if (skb_linearize(buf) != 0) {
			dev_kfree_skb_any(buf);
			goto error;
		}

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb) {
			dev_kfree_skb_any(buf);
			goto error;
		}

		entry = (struct skb_data *)buf->cb;
		entry->urb = urb;
		entry->dev = dev;
		entry->length = 0;
		entry->num_of_packet = 0;

		skb_queue_tail(buf_pool, buf);
	}

	return 0;

error:
	lan78xx_free_buf_pool(buf_pool);

	return -ENOMEM;
}

static struct sk_buff *lan78xx_get_rx_buf(struct lan78xx_net *dev)
{
	return lan78xx_get_buf(&dev->rxq_free);
}

static void lan78xx_release_rx_buf(struct lan78xx_net *dev,
				   struct sk_buff *rx_buf)
{
	lan78xx_release_buf(&dev->rxq_free, rx_buf);
}

static void lan78xx_free_rx_resources(struct lan78xx_net *dev)
{
	lan78xx_free_buf_pool(&dev->rxq_free);
}

static int lan78xx_alloc_rx_resources(struct lan78xx_net *dev)
{
	return lan78xx_alloc_buf_pool(&dev->rxq_free,
				      dev->n_rx_urbs, dev->rx_urb_size, dev);
}

static struct sk_buff *lan78xx_get_tx_buf(struct lan78xx_net *dev)
{
	return lan78xx_get_buf(&dev->txq_free);
}

static void lan78xx_release_tx_buf(struct lan78xx_net *dev,
				   struct sk_buff *tx_buf)
{
	lan78xx_release_buf(&dev->txq_free, tx_buf);
}

static void lan78xx_free_tx_resources(struct lan78xx_net *dev)
{
	lan78xx_free_buf_pool(&dev->txq_free);
}

static int lan78xx_alloc_tx_resources(struct lan78xx_net *dev)
{
	return lan78xx_alloc_buf_pool(&dev->txq_free,
				      dev->n_tx_urbs, dev->tx_urb_size, dev);
}

static int lan78xx_read_reg(struct lan78xx_net *dev, u32 index, u32 *data)
{
	u32 *buf;
	int ret;

	if (test_bit(EVENT_DEV_DISCONNECT, &dev->flags))
		return -ENODEV;

	buf = kmalloc(sizeof(u32), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
			      USB_VENDOR_REQUEST_READ_REGISTER,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0, index, buf, 4, USB_CTRL_GET_TIMEOUT);
	if (likely(ret >= 0)) {
		le32_to_cpus(buf);
		*data = *buf;
	} else if (net_ratelimit()) {
		netdev_warn(dev->net,
			    "Failed to read register index 0x%08x. ret = %pe",
			    index, ERR_PTR(ret));
	}

	kfree(buf);

	return ret < 0 ? ret : 0;
}

static int lan78xx_write_reg(struct lan78xx_net *dev, u32 index, u32 data)
{
	u32 *buf;
	int ret;

	if (test_bit(EVENT_DEV_DISCONNECT, &dev->flags))
		return -ENODEV;

	buf = kmalloc(sizeof(u32), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	*buf = data;
	cpu_to_le32s(buf);

	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
			      USB_VENDOR_REQUEST_WRITE_REGISTER,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0, index, buf, 4, USB_CTRL_SET_TIMEOUT);
	if (unlikely(ret < 0) &&
	    net_ratelimit()) {
		netdev_warn(dev->net,
			    "Failed to write register index 0x%08x. ret = %pe",
			    index, ERR_PTR(ret));
	}

	kfree(buf);

	return ret < 0 ? ret : 0;
}

static int lan78xx_update_reg(struct lan78xx_net *dev, u32 reg, u32 mask,
			      u32 data)
{
	int ret;
	u32 buf;

	ret = lan78xx_read_reg(dev, reg, &buf);
	if (ret < 0)
		return ret;

	buf &= ~mask;
	buf |= (mask & data);

	return lan78xx_write_reg(dev, reg, buf);
}

static int lan78xx_read_stats(struct lan78xx_net *dev,
			      struct lan78xx_statstage *data)
{
	int ret = 0;
	int i;
	struct lan78xx_statstage *stats;
	u32 *src;
	u32 *dst;

	stats = kmalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	ret = usb_control_msg(dev->udev,
			      usb_rcvctrlpipe(dev->udev, 0),
			      USB_VENDOR_REQUEST_GET_STATS,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0,
			      0,
			      (void *)stats,
			      sizeof(*stats),
			      USB_CTRL_SET_TIMEOUT);
	if (likely(ret >= 0)) {
		src = (u32 *)stats;
		dst = (u32 *)data;
		for (i = 0; i < sizeof(*stats) / sizeof(u32); i++) {
			le32_to_cpus(&src[i]);
			dst[i] = src[i];
		}
	} else {
		netdev_warn(dev->net,
			    "Failed to read stat ret = %d", ret);
	}

	kfree(stats);

	return ret;
}

#define check_counter_rollover(struct1, dev_stats, member)		\
	do {								\
		if ((struct1)->member < (dev_stats).saved.member)	\
			(dev_stats).rollover_count.member++;		\
	} while (0)

static void lan78xx_check_stat_rollover(struct lan78xx_net *dev,
					struct lan78xx_statstage *stats)
{
	check_counter_rollover(stats, dev->stats, rx_fcs_errors);
	check_counter_rollover(stats, dev->stats, rx_alignment_errors);
	check_counter_rollover(stats, dev->stats, rx_fragment_errors);
	check_counter_rollover(stats, dev->stats, rx_jabber_errors);
	check_counter_rollover(stats, dev->stats, rx_undersize_frame_errors);
	check_counter_rollover(stats, dev->stats, rx_oversize_frame_errors);
	check_counter_rollover(stats, dev->stats, rx_dropped_frames);
	check_counter_rollover(stats, dev->stats, rx_unicast_byte_count);
	check_counter_rollover(stats, dev->stats, rx_broadcast_byte_count);
	check_counter_rollover(stats, dev->stats, rx_multicast_byte_count);
	check_counter_rollover(stats, dev->stats, rx_unicast_frames);
	check_counter_rollover(stats, dev->stats, rx_broadcast_frames);
	check_counter_rollover(stats, dev->stats, rx_multicast_frames);
	check_counter_rollover(stats, dev->stats, rx_pause_frames);
	check_counter_rollover(stats, dev->stats, rx_64_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_65_127_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_128_255_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_256_511_bytes_frames);
	check_counter_rollover(stats, dev->stats, rx_512_1023_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_1024_1518_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_greater_1518_byte_frames);
	check_counter_rollover(stats, dev->stats, eee_rx_lpi_transitions);
	check_counter_rollover(stats, dev->stats, eee_rx_lpi_time);
	check_counter_rollover(stats, dev->stats, tx_fcs_errors);
	check_counter_rollover(stats, dev->stats, tx_excess_deferral_errors);
	check_counter_rollover(stats, dev->stats, tx_carrier_errors);
	check_counter_rollover(stats, dev->stats, tx_bad_byte_count);
	check_counter_rollover(stats, dev->stats, tx_single_collisions);
	check_counter_rollover(stats, dev->stats, tx_multiple_collisions);
	check_counter_rollover(stats, dev->stats, tx_excessive_collision);
	check_counter_rollover(stats, dev->stats, tx_late_collisions);
	check_counter_rollover(stats, dev->stats, tx_unicast_byte_count);
	check_counter_rollover(stats, dev->stats, tx_broadcast_byte_count);
	check_counter_rollover(stats, dev->stats, tx_multicast_byte_count);
	check_counter_rollover(stats, dev->stats, tx_unicast_frames);
	check_counter_rollover(stats, dev->stats, tx_broadcast_frames);
	check_counter_rollover(stats, dev->stats, tx_multicast_frames);
	check_counter_rollover(stats, dev->stats, tx_pause_frames);
	check_counter_rollover(stats, dev->stats, tx_64_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_65_127_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_128_255_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_256_511_bytes_frames);
	check_counter_rollover(stats, dev->stats, tx_512_1023_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_1024_1518_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_greater_1518_byte_frames);
	check_counter_rollover(stats, dev->stats, eee_tx_lpi_transitions);
	check_counter_rollover(stats, dev->stats, eee_tx_lpi_time);

	memcpy(&dev->stats.saved, stats, sizeof(struct lan78xx_statstage));
}

static void lan78xx_update_stats(struct lan78xx_net *dev)
{
	u32 *p, *count, *max;
	u64 *data;
	int i;
	struct lan78xx_statstage lan78xx_stats;

	if (usb_autopm_get_interface(dev->intf) < 0)
		return;

	p = (u32 *)&lan78xx_stats;
	count = (u32 *)&dev->stats.rollover_count;
	max = (u32 *)&dev->stats.rollover_max;
	data = (u64 *)&dev->stats.curr_stat;

	mutex_lock(&dev->stats.access_lock);

	if (lan78xx_read_stats(dev, &lan78xx_stats) > 0)
		lan78xx_check_stat_rollover(dev, &lan78xx_stats);

	for (i = 0; i < (sizeof(lan78xx_stats) / (sizeof(u32))); i++)
		data[i] = (u64)p[i] + ((u64)count[i] * ((u64)max[i] + 1));

	mutex_unlock(&dev->stats.access_lock);

	usb_autopm_put_interface(dev->intf);
}

static int lan78xx_start_hw(struct lan78xx_net *dev, u32 reg, u32 hw_enable)
{
	return lan78xx_update_reg(dev, reg, hw_enable, hw_enable);
}

static int lan78xx_stop_hw(struct lan78xx_net *dev, u32 reg, u32 hw_enabled,
			   u32 hw_disabled)
{
	unsigned long timeout;
	bool stopped = true;
	int ret;
	u32 buf;

	/* Stop the h/w block (if not already stopped) */

	ret = lan78xx_read_reg(dev, reg, &buf);
	if (ret < 0)
		return ret;

	if (buf & hw_enabled) {
		buf &= ~hw_enabled;

		ret = lan78xx_write_reg(dev, reg, buf);
		if (ret < 0)
			return ret;

		stopped = false;
		timeout = jiffies + HW_DISABLE_TIMEOUT;
		do  {
			ret = lan78xx_read_reg(dev, reg, &buf);
			if (ret < 0)
				return ret;

			if (buf & hw_disabled)
				stopped = true;
			else
				msleep(HW_DISABLE_DELAY_MS);
		} while (!stopped && !time_after(jiffies, timeout));
	}

	return stopped ? 0 : -ETIMEDOUT;
}

static int lan78xx_flush_fifo(struct lan78xx_net *dev, u32 reg, u32 fifo_flush)
{
	return lan78xx_update_reg(dev, reg, fifo_flush, fifo_flush);
}

static int lan78xx_start_tx_path(struct lan78xx_net *dev)
{
	int ret;

	netif_dbg(dev, drv, dev->net, "start tx path");

	/* Start the MAC transmitter */

	ret = lan78xx_start_hw(dev, MAC_TX, MAC_TX_TXEN_);
	if (ret < 0)
		return ret;

	/* Start the Tx FIFO */

	ret = lan78xx_start_hw(dev, FCT_TX_CTL, FCT_TX_CTL_EN_);
	if (ret < 0)
		return ret;

	return 0;
}

static int lan78xx_stop_tx_path(struct lan78xx_net *dev)
{
	int ret;

	netif_dbg(dev, drv, dev->net, "stop tx path");

	/* Stop the Tx FIFO */

	ret = lan78xx_stop_hw(dev, FCT_TX_CTL, FCT_TX_CTL_EN_, FCT_TX_CTL_DIS_);
	if (ret < 0)
		return ret;

	/* Stop the MAC transmitter */

	ret = lan78xx_stop_hw(dev, MAC_TX, MAC_TX_TXEN_, MAC_TX_TXD_);
	if (ret < 0)
		return ret;

	return 0;
}

/* The caller must ensure the Tx path is stopped before calling
 * lan78xx_flush_tx_fifo().
 */
static int lan78xx_flush_tx_fifo(struct lan78xx_net *dev)
{
	return lan78xx_flush_fifo(dev, FCT_TX_CTL, FCT_TX_CTL_RST_);
}

static int lan78xx_start_rx_path(struct lan78xx_net *dev)
{
	int ret;

	netif_dbg(dev, drv, dev->net, "start rx path");

	/* Start the Rx FIFO */

	ret = lan78xx_start_hw(dev, FCT_RX_CTL, FCT_RX_CTL_EN_);
	if (ret < 0)
		return ret;

	/* Start the MAC receiver*/

	ret = lan78xx_start_hw(dev, MAC_RX, MAC_RX_RXEN_);
	if (ret < 0)
		return ret;

	return 0;
}

static int lan78xx_stop_rx_path(struct lan78xx_net *dev)
{
	int ret;

	netif_dbg(dev, drv, dev->net, "stop rx path");

	/* Stop the MAC receiver */

	ret = lan78xx_stop_hw(dev, MAC_RX, MAC_RX_RXEN_, MAC_RX_RXD_);
	if (ret < 0)
		return ret;

	/* Stop the Rx FIFO */

	ret = lan78xx_stop_hw(dev, FCT_RX_CTL, FCT_RX_CTL_EN_, FCT_RX_CTL_DIS_);
	if (ret < 0)
		return ret;

	return 0;
}

/* The caller must ensure the Rx path is stopped before calling
 * lan78xx_flush_rx_fifo().
 */
static int lan78xx_flush_rx_fifo(struct lan78xx_net *dev)
{
	return lan78xx_flush_fifo(dev, FCT_RX_CTL, FCT_RX_CTL_RST_);
}

/* Loop until the read is completed with timeout called with mdiobus_mutex held */
static int lan78xx_mdiobus_wait_not_busy(struct lan78xx_net *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = lan78xx_read_reg(dev, MII_ACC, &val);
		if (ret < 0)
			return ret;

		if (!(val & MII_ACC_MII_BUSY_))
			return 0;
	} while (!time_after(jiffies, start_time + HZ));

	return -ETIMEDOUT;
}

static inline u32 mii_access(int id, int index, int read)
{
	u32 ret;

	ret = ((u32)id << MII_ACC_PHY_ADDR_SHIFT_) & MII_ACC_PHY_ADDR_MASK_;
	ret |= ((u32)index << MII_ACC_MIIRINDA_SHIFT_) & MII_ACC_MIIRINDA_MASK_;
	if (read)
		ret |= MII_ACC_MII_READ_;
	else
		ret |= MII_ACC_MII_WRITE_;
	ret |= MII_ACC_MII_BUSY_;

	return ret;
}

static int lan78xx_wait_eeprom(struct lan78xx_net *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = lan78xx_read_reg(dev, E2P_CMD, &val);
		if (ret < 0)
			return ret;

		if (!(val & E2P_CMD_EPC_BUSY_) ||
		    (val & E2P_CMD_EPC_TIMEOUT_))
			break;
		usleep_range(40, 100);
	} while (!time_after(jiffies, start_time + HZ));

	if (val & (E2P_CMD_EPC_TIMEOUT_ | E2P_CMD_EPC_BUSY_)) {
		netdev_warn(dev->net, "EEPROM read operation timeout");
		return -ETIMEDOUT;
	}

	return 0;
}

static int lan78xx_eeprom_confirm_not_busy(struct lan78xx_net *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = lan78xx_read_reg(dev, E2P_CMD, &val);
		if (ret < 0)
			return ret;

		if (!(val & E2P_CMD_EPC_BUSY_))
			return 0;

		usleep_range(40, 100);
	} while (!time_after(jiffies, start_time + HZ));

	netdev_warn(dev->net, "EEPROM is busy");
	return -ETIMEDOUT;
}

static int lan78xx_read_raw_eeprom(struct lan78xx_net *dev, u32 offset,
				   u32 length, u8 *data)
{
	u32 val, saved;
	int i, ret;

	/* depends on chip, some EEPROM pins are muxed with LED function.
	 * disable & restore LED function to access EEPROM.
	 */
	ret = lan78xx_read_reg(dev, HW_CFG, &val);
	if (ret < 0)
		return ret;

	saved = val;
	if (dev->chipid == ID_REV_CHIP_ID_7800_) {
		val &= ~(HW_CFG_LED1_EN_ | HW_CFG_LED0_EN_);
		ret = lan78xx_write_reg(dev, HW_CFG, val);
		if (ret < 0)
			return ret;
	}

	ret = lan78xx_eeprom_confirm_not_busy(dev);
	if (ret == -ETIMEDOUT)
		goto read_raw_eeprom_done;
	/* If USB fails, there is nothing to do */
	if (ret < 0)
		return ret;

	for (i = 0; i < length; i++) {
		val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_READ_;
		val |= (offset & E2P_CMD_EPC_ADDR_MASK_);
		ret = lan78xx_write_reg(dev, E2P_CMD, val);
		if (ret < 0)
			return ret;

		ret = lan78xx_wait_eeprom(dev);
		/* Looks like not USB specific error, try to recover */
		if (ret == -ETIMEDOUT)
			goto read_raw_eeprom_done;
		/* If USB fails, there is nothing to do */
		if (ret < 0)
			return ret;

		ret = lan78xx_read_reg(dev, E2P_DATA, &val);
		if (ret < 0)
			return ret;

		data[i] = val & 0xFF;
		offset++;
	}

read_raw_eeprom_done:
	if (dev->chipid == ID_REV_CHIP_ID_7800_) {
		int rc = lan78xx_write_reg(dev, HW_CFG, saved);
		/* If USB fails, there is nothing to do */
		if (rc < 0)
			return rc;
	}
	return ret;
}

static int lan78xx_read_eeprom(struct lan78xx_net *dev, u32 offset,
			       u32 length, u8 *data)
{
	int ret;
	u8 sig;

	ret = lan78xx_read_raw_eeprom(dev, 0, 1, &sig);
	if (ret < 0)
		return ret;

	if (sig != EEPROM_INDICATOR)
		return -ENODATA;

	return lan78xx_read_raw_eeprom(dev, offset, length, data);
}

static int lan78xx_write_raw_eeprom(struct lan78xx_net *dev, u32 offset,
				    u32 length, u8 *data)
{
	u32 val;
	u32 saved;
	int i, ret;

	/* depends on chip, some EEPROM pins are muxed with LED function.
	 * disable & restore LED function to access EEPROM.
	 */
	ret = lan78xx_read_reg(dev, HW_CFG, &val);
	if (ret < 0)
		return ret;

	saved = val;
	if (dev->chipid == ID_REV_CHIP_ID_7800_) {
		val &= ~(HW_CFG_LED1_EN_ | HW_CFG_LED0_EN_);
		ret = lan78xx_write_reg(dev, HW_CFG, val);
		if (ret < 0)
			return ret;
	}

	ret = lan78xx_eeprom_confirm_not_busy(dev);
	/* Looks like not USB specific error, try to recover */
	if (ret == -ETIMEDOUT)
		goto write_raw_eeprom_done;
	/* If USB fails, there is nothing to do */
	if (ret < 0)
		return ret;

	/* Issue write/erase enable command */
	val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_EWEN_;
	ret = lan78xx_write_reg(dev, E2P_CMD, val);
	if (ret < 0)
		return ret;

	ret = lan78xx_wait_eeprom(dev);
	/* Looks like not USB specific error, try to recover */
	if (ret == -ETIMEDOUT)
		goto write_raw_eeprom_done;
	/* If USB fails, there is nothing to do */
	if (ret < 0)
		return ret;

	for (i = 0; i < length; i++) {
		/* Fill data register */
		val = data[i];
		ret = lan78xx_write_reg(dev, E2P_DATA, val);
		if (ret < 0)
			return ret;

		/* Send "write" command */
		val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_WRITE_;
		val |= (offset & E2P_CMD_EPC_ADDR_MASK_);
		ret = lan78xx_write_reg(dev, E2P_CMD, val);
		if (ret < 0)
			return ret;

		ret = lan78xx_wait_eeprom(dev);
		/* Looks like not USB specific error, try to recover */
		if (ret == -ETIMEDOUT)
			goto write_raw_eeprom_done;
		/* If USB fails, there is nothing to do */
		if (ret < 0)
			return ret;

		offset++;
	}

write_raw_eeprom_done:
	if (dev->chipid == ID_REV_CHIP_ID_7800_)
		return lan78xx_write_reg(dev, HW_CFG, saved);

	return 0;
}

static int lan78xx_read_raw_otp(struct lan78xx_net *dev, u32 offset,
				u32 length, u8 *data)
{
	unsigned long timeout;
	int ret, i;
	u32 buf;

	ret = lan78xx_read_reg(dev, OTP_PWR_DN, &buf);
	if (ret < 0)
		return ret;

	if (buf & OTP_PWR_DN_PWRDN_N_) {
		/* clear it and wait to be cleared */
		ret = lan78xx_write_reg(dev, OTP_PWR_DN, 0);
		if (ret < 0)
			return ret;

		timeout = jiffies + HZ;
		do {
			usleep_range(1, 10);
			ret = lan78xx_read_reg(dev, OTP_PWR_DN, &buf);
			if (ret < 0)
				return ret;

			if (time_after(jiffies, timeout)) {
				netdev_warn(dev->net,
					    "timeout on OTP_PWR_DN");
				return -ETIMEDOUT;
			}
		} while (buf & OTP_PWR_DN_PWRDN_N_);
	}

	for (i = 0; i < length; i++) {
		ret = lan78xx_write_reg(dev, OTP_ADDR1,
					((offset + i) >> 8) & OTP_ADDR1_15_11);
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, OTP_ADDR2,
					((offset + i) & OTP_ADDR2_10_3));
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, OTP_FUNC_CMD, OTP_FUNC_CMD_READ_);
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, OTP_CMD_GO, OTP_CMD_GO_GO_);
		if (ret < 0)
			return ret;

		timeout = jiffies + HZ;
		do {
			udelay(1);
			ret = lan78xx_read_reg(dev, OTP_STATUS, &buf);
			if (ret < 0)
				return ret;

			if (time_after(jiffies, timeout)) {
				netdev_warn(dev->net,
					    "timeout on OTP_STATUS");
				return -ETIMEDOUT;
			}
		} while (buf & OTP_STATUS_BUSY_);

		ret = lan78xx_read_reg(dev, OTP_RD_DATA, &buf);
		if (ret < 0)
			return ret;

		data[i] = (u8)(buf & 0xFF);
	}

	return 0;
}

static int lan78xx_write_raw_otp(struct lan78xx_net *dev, u32 offset,
				 u32 length, u8 *data)
{
	int i;
	u32 buf;
	unsigned long timeout;
	int ret;

	ret = lan78xx_read_reg(dev, OTP_PWR_DN, &buf);
	if (ret < 0)
		return ret;

	if (buf & OTP_PWR_DN_PWRDN_N_) {
		/* clear it and wait to be cleared */
		ret = lan78xx_write_reg(dev, OTP_PWR_DN, 0);
		if (ret < 0)
			return ret;

		timeout = jiffies + HZ;
		do {
			udelay(1);
			ret = lan78xx_read_reg(dev, OTP_PWR_DN, &buf);
			if (ret < 0)
				return ret;

			if (time_after(jiffies, timeout)) {
				netdev_warn(dev->net,
					    "timeout on OTP_PWR_DN completion");
				return -ETIMEDOUT;
			}
		} while (buf & OTP_PWR_DN_PWRDN_N_);
	}

	/* set to BYTE program mode */
	ret = lan78xx_write_reg(dev, OTP_PRGM_MODE, OTP_PRGM_MODE_BYTE_);
	if (ret < 0)
		return ret;

	for (i = 0; i < length; i++) {
		ret = lan78xx_write_reg(dev, OTP_ADDR1,
					((offset + i) >> 8) & OTP_ADDR1_15_11);
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, OTP_ADDR2,
					((offset + i) & OTP_ADDR2_10_3));
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, OTP_PRGM_DATA, data[i]);
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, OTP_TST_CMD, OTP_TST_CMD_PRGVRFY_);
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, OTP_CMD_GO, OTP_CMD_GO_GO_);
		if (ret < 0)
			return ret;

		timeout = jiffies + HZ;
		do {
			udelay(1);
			ret = lan78xx_read_reg(dev, OTP_STATUS, &buf);
			if (ret < 0)
				return ret;

			if (time_after(jiffies, timeout)) {
				netdev_warn(dev->net,
					    "Timeout on OTP_STATUS completion");
				return -ETIMEDOUT;
			}
		} while (buf & OTP_STATUS_BUSY_);
	}

	return 0;
}

static int lan78xx_read_otp(struct lan78xx_net *dev, u32 offset,
			    u32 length, u8 *data)
{
	u8 sig;
	int ret;

	ret = lan78xx_read_raw_otp(dev, 0, 1, &sig);

	if (ret == 0) {
		if (sig == OTP_INDICATOR_2)
			offset += 0x100;
		else if (sig != OTP_INDICATOR_1)
			ret = -EINVAL;
		if (!ret)
			ret = lan78xx_read_raw_otp(dev, offset, length, data);
	}

	return ret;
}

static int lan78xx_dataport_wait_not_busy(struct lan78xx_net *dev)
{
	int i, ret;

	for (i = 0; i < 100; i++) {
		u32 dp_sel;

		ret = lan78xx_read_reg(dev, DP_SEL, &dp_sel);
		if (unlikely(ret < 0))
			return ret;

		if (dp_sel & DP_SEL_DPRDY_)
			return 0;

		usleep_range(40, 100);
	}

	netdev_warn(dev->net, "%s timed out", __func__);

	return -ETIMEDOUT;
}

static int lan78xx_dataport_write(struct lan78xx_net *dev, u32 ram_select,
				  u32 addr, u32 length, u32 *buf)
{
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	int i, ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	mutex_lock(&pdata->dataport_mutex);

	ret = lan78xx_dataport_wait_not_busy(dev);
	if (ret < 0)
		goto dataport_write;

	ret = lan78xx_update_reg(dev, DP_SEL, DP_SEL_RSEL_MASK_, ram_select);
	if (ret < 0)
		goto dataport_write;

	for (i = 0; i < length; i++) {
		ret = lan78xx_write_reg(dev, DP_ADDR, addr + i);
		if (ret < 0)
			goto dataport_write;

		ret = lan78xx_write_reg(dev, DP_DATA, buf[i]);
		if (ret < 0)
			goto dataport_write;

		ret = lan78xx_write_reg(dev, DP_CMD, DP_CMD_WRITE_);
		if (ret < 0)
			goto dataport_write;

		ret = lan78xx_dataport_wait_not_busy(dev);
		if (ret < 0)
			goto dataport_write;
	}

dataport_write:
	if (ret < 0)
		netdev_warn(dev->net, "dataport write failed %pe", ERR_PTR(ret));

	mutex_unlock(&pdata->dataport_mutex);
	usb_autopm_put_interface(dev->intf);

	return ret;
}

static void lan78xx_set_addr_filter(struct lan78xx_priv *pdata,
				    int index, u8 addr[ETH_ALEN])
{
	u32 temp;

	if ((pdata) && (index > 0) && (index < NUM_OF_MAF)) {
		temp = addr[3];
		temp = addr[2] | (temp << 8);
		temp = addr[1] | (temp << 8);
		temp = addr[0] | (temp << 8);
		pdata->pfilter_table[index][1] = temp;
		temp = addr[5];
		temp = addr[4] | (temp << 8);
		temp |= MAF_HI_VALID_ | MAF_HI_TYPE_DST_;
		pdata->pfilter_table[index][0] = temp;
	}
}

/* returns hash bit number for given MAC address */
static inline u32 lan78xx_hash(char addr[ETH_ALEN])
{
	return (ether_crc(ETH_ALEN, addr) >> 23) & 0x1ff;
}

static void lan78xx_deferred_multicast_write(struct work_struct *param)
{
	struct lan78xx_priv *pdata =
			container_of(param, struct lan78xx_priv, set_multicast);
	struct lan78xx_net *dev = pdata->dev;
	int i, ret;

	netif_dbg(dev, drv, dev->net, "deferred multicast write 0x%08x\n",
		  pdata->rfe_ctl);

	ret = lan78xx_dataport_write(dev, DP_SEL_RSEL_VLAN_DA_,
				     DP_SEL_VHF_VLAN_LEN,
				     DP_SEL_VHF_HASH_LEN, pdata->mchash_table);
	if (ret < 0)
		goto multicast_write_done;

	for (i = 1; i < NUM_OF_MAF; i++) {
		ret = lan78xx_write_reg(dev, MAF_HI(i), 0);
		if (ret < 0)
			goto multicast_write_done;

		ret = lan78xx_write_reg(dev, MAF_LO(i),
					pdata->pfilter_table[i][1]);
		if (ret < 0)
			goto multicast_write_done;

		ret = lan78xx_write_reg(dev, MAF_HI(i),
					pdata->pfilter_table[i][0]);
		if (ret < 0)
			goto multicast_write_done;
	}

	ret = lan78xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);

multicast_write_done:
	if (ret < 0)
		netdev_warn(dev->net, "multicast write failed %pe", ERR_PTR(ret));
	return;
}

static void lan78xx_set_multicast(struct net_device *netdev)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&pdata->rfe_ctl_lock, flags);

	pdata->rfe_ctl &= ~(RFE_CTL_UCAST_EN_ | RFE_CTL_MCAST_EN_ |
			    RFE_CTL_DA_PERFECT_ | RFE_CTL_MCAST_HASH_);

	for (i = 0; i < DP_SEL_VHF_HASH_LEN; i++)
		pdata->mchash_table[i] = 0;

	/* pfilter_table[0] has own HW address */
	for (i = 1; i < NUM_OF_MAF; i++) {
		pdata->pfilter_table[i][0] = 0;
		pdata->pfilter_table[i][1] = 0;
	}

	pdata->rfe_ctl |= RFE_CTL_BCAST_EN_;

	if (dev->net->flags & IFF_PROMISC) {
		netif_dbg(dev, drv, dev->net, "promiscuous mode enabled");
		pdata->rfe_ctl |= RFE_CTL_MCAST_EN_ | RFE_CTL_UCAST_EN_;
	} else {
		if (dev->net->flags & IFF_ALLMULTI) {
			netif_dbg(dev, drv, dev->net,
				  "receive all multicast enabled");
			pdata->rfe_ctl |= RFE_CTL_MCAST_EN_;
		}
	}

	if (netdev_mc_count(dev->net)) {
		struct netdev_hw_addr *ha;
		int i;

		netif_dbg(dev, drv, dev->net, "receive multicast hash filter");

		pdata->rfe_ctl |= RFE_CTL_DA_PERFECT_;

		i = 1;
		netdev_for_each_mc_addr(ha, netdev) {
			/* set first 32 into Perfect Filter */
			if (i < 33) {
				lan78xx_set_addr_filter(pdata, i, ha->addr);
			} else {
				u32 bitnum = lan78xx_hash(ha->addr);

				pdata->mchash_table[bitnum / 32] |=
							(1 << (bitnum % 32));
				pdata->rfe_ctl |= RFE_CTL_MCAST_HASH_;
			}
			i++;
		}
	}

	spin_unlock_irqrestore(&pdata->rfe_ctl_lock, flags);

	/* defer register writes to a sleepable context */
	schedule_work(&pdata->set_multicast);
}

static void lan78xx_rx_urb_submit_all(struct lan78xx_net *dev);

static int lan78xx_mac_reset(struct lan78xx_net *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	mutex_lock(&dev->mdiobus_mutex);

	/* Resetting the device while there is activity on the MDIO
	 * bus can result in the MAC interface locking up and not
	 * completing register access transactions.
	 */
	ret = lan78xx_mdiobus_wait_not_busy(dev);
	if (ret < 0)
		goto exit_unlock;

	ret = lan78xx_read_reg(dev, MAC_CR, &val);
	if (ret < 0)
		goto exit_unlock;

	val |= MAC_CR_RST_;
	ret = lan78xx_write_reg(dev, MAC_CR, val);
	if (ret < 0)
		goto exit_unlock;

	/* Wait for the reset to complete before allowing any further
	 * MAC register accesses otherwise the MAC may lock up.
	 */
	do {
		ret = lan78xx_read_reg(dev, MAC_CR, &val);
		if (ret < 0)
			goto exit_unlock;

		if (!(val & MAC_CR_RST_)) {
			ret = 0;
			goto exit_unlock;
		}
	} while (!time_after(jiffies, start_time + HZ));

	ret = -ETIMEDOUT;
exit_unlock:
	mutex_unlock(&dev->mdiobus_mutex);

	return ret;
}

/**
 * lan78xx_phy_int_ack - Acknowledge PHY interrupt
 * @dev: pointer to the LAN78xx device structure
 *
 * This function acknowledges the PHY interrupt by setting the
 * INT_STS_PHY_INT_ bit in the interrupt status register (INT_STS).
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int lan78xx_phy_int_ack(struct lan78xx_net *dev)
{
	return lan78xx_write_reg(dev, INT_STS, INT_STS_PHY_INT_);
}

/* some work can't be done in tasklets, so we use keventd
 *
 * NOTE:  annoying asymmetry:  if it's active, schedule_work() fails,
 * but tasklet_schedule() doesn't.	hope the failure is rare.
 */
static void lan78xx_defer_kevent(struct lan78xx_net *dev, int work)
{
	set_bit(work, &dev->flags);
	if (!schedule_delayed_work(&dev->wq, 0))
		netdev_err(dev->net, "kevent %d may have been dropped\n", work);
}

static void lan78xx_status(struct lan78xx_net *dev, struct urb *urb)
{
	u32 intdata;

	if (urb->actual_length != 4) {
		netdev_warn(dev->net,
			    "unexpected urb length %d", urb->actual_length);
		return;
	}

	intdata = get_unaligned_le32(urb->transfer_buffer);

	if (intdata & INT_ENP_PHY_INT) {
		netif_dbg(dev, link, dev->net, "PHY INTR: 0x%08x\n", intdata);
		lan78xx_defer_kevent(dev, EVENT_PHY_INT_ACK);

		if (dev->domain_data.phyirq > 0)
			generic_handle_irq_safe(dev->domain_data.phyirq);
	} else {
		netdev_warn(dev->net,
			    "unexpected interrupt: 0x%08x\n", intdata);
	}
}

static int lan78xx_ethtool_get_eeprom_len(struct net_device *netdev)
{
	return MAX_EEPROM_SIZE;
}

static int lan78xx_ethtool_get_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret)
		return ret;

	ee->magic = LAN78XX_EEPROM_MAGIC;

	ret = lan78xx_read_raw_eeprom(dev, ee->offset, ee->len, data);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_ethtool_set_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret)
		return ret;

	/* Invalid EEPROM_INDICATOR at offset zero will result in a failure
	 * to load data from EEPROM
	 */
	if (ee->magic == LAN78XX_EEPROM_MAGIC)
		ret = lan78xx_write_raw_eeprom(dev, ee->offset, ee->len, data);
	else if ((ee->magic == LAN78XX_OTP_MAGIC) &&
		 (ee->offset == 0) &&
		 (ee->len == 512) &&
		 (data[0] == OTP_INDICATOR_1))
		ret = lan78xx_write_raw_otp(dev, ee->offset, ee->len, data);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static void lan78xx_get_strings(struct net_device *netdev, u32 stringset,
				u8 *data)
{
	if (stringset == ETH_SS_STATS)
		memcpy(data, lan78xx_gstrings, sizeof(lan78xx_gstrings));
}

static int lan78xx_get_sset_count(struct net_device *netdev, int sset)
{
	if (sset == ETH_SS_STATS)
		return ARRAY_SIZE(lan78xx_gstrings);
	else
		return -EOPNOTSUPP;
}

static void lan78xx_get_stats(struct net_device *netdev,
			      struct ethtool_stats *stats, u64 *data)
{
	struct lan78xx_net *dev = netdev_priv(netdev);

	lan78xx_update_stats(dev);

	mutex_lock(&dev->stats.access_lock);
	memcpy(data, &dev->stats.curr_stat, sizeof(dev->stats.curr_stat));
	mutex_unlock(&dev->stats.access_lock);
}

static void lan78xx_get_wol(struct net_device *netdev,
			    struct ethtool_wolinfo *wol)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	int ret;
	u32 buf;
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);

	if (usb_autopm_get_interface(dev->intf) < 0)
		return;

	ret = lan78xx_read_reg(dev, USB_CFG0, &buf);
	if (unlikely(ret < 0)) {
		netdev_warn(dev->net, "failed to get WoL %pe", ERR_PTR(ret));
		wol->supported = 0;
		wol->wolopts = 0;
	} else {
		if (buf & USB_CFG_RMT_WKP_) {
			wol->supported = WAKE_ALL;
			wol->wolopts = pdata->wol;
		} else {
			wol->supported = 0;
			wol->wolopts = 0;
		}
	}

	usb_autopm_put_interface(dev->intf);
}

static int lan78xx_set_wol(struct net_device *netdev,
			   struct ethtool_wolinfo *wol)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	int ret;

	if (wol->wolopts & ~WAKE_ALL)
		return -EINVAL;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	pdata->wol = wol->wolopts;

	ret = device_set_wakeup_enable(&dev->udev->dev, (bool)wol->wolopts);
	if (ret < 0)
		goto exit_pm_put;

	ret = phy_ethtool_set_wol(netdev->phydev, wol);

exit_pm_put:
	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_get_eee(struct net_device *net, struct ethtool_keee *edata)
{
	struct lan78xx_net *dev = netdev_priv(net);

	return phylink_ethtool_get_eee(dev->phylink, edata);
}

static int lan78xx_set_eee(struct net_device *net, struct ethtool_keee *edata)
{
	struct lan78xx_net *dev = netdev_priv(net);

	return phylink_ethtool_set_eee(dev->phylink, edata);
}

static void lan78xx_get_drvinfo(struct net_device *net,
				struct ethtool_drvinfo *info)
{
	struct lan78xx_net *dev = netdev_priv(net);

	strscpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	usb_make_path(dev->udev, info->bus_info, sizeof(info->bus_info));
}

static u32 lan78xx_get_msglevel(struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);

	return dev->msg_enable;
}

static void lan78xx_set_msglevel(struct net_device *net, u32 level)
{
	struct lan78xx_net *dev = netdev_priv(net);

	dev->msg_enable = level;
}

static int lan78xx_get_link_ksettings(struct net_device *net,
				      struct ethtool_link_ksettings *cmd)
{
	struct lan78xx_net *dev = netdev_priv(net);

	return phylink_ethtool_ksettings_get(dev->phylink, cmd);
}

static int lan78xx_set_link_ksettings(struct net_device *net,
				      const struct ethtool_link_ksettings *cmd)
{
	struct lan78xx_net *dev = netdev_priv(net);

	return phylink_ethtool_ksettings_set(dev->phylink, cmd);
}

static void lan78xx_get_pause(struct net_device *net,
			      struct ethtool_pauseparam *pause)
{
	struct lan78xx_net *dev = netdev_priv(net);

	phylink_ethtool_get_pauseparam(dev->phylink, pause);
}

static int lan78xx_set_pause(struct net_device *net,
			     struct ethtool_pauseparam *pause)
{
	struct lan78xx_net *dev = netdev_priv(net);

	return phylink_ethtool_set_pauseparam(dev->phylink, pause);
}

static int lan78xx_get_regs_len(struct net_device *netdev)
{
	return sizeof(lan78xx_regs);
}

static void
lan78xx_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
		 void *buf)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	unsigned int data_count = 0;
	u32 *data = buf;
	int i, ret;

	/* Read Device/MAC registers */
	for (i = 0; i < ARRAY_SIZE(lan78xx_regs); i++) {
		ret = lan78xx_read_reg(dev, lan78xx_regs[i], &data[i]);
		if (ret < 0) {
			netdev_warn(dev->net,
				    "failed to read register 0x%08x\n",
				    lan78xx_regs[i]);
			goto clean_data;
		}

		data_count++;
	}

	return;

clean_data:
	memset(data, 0, data_count * sizeof(u32));
}

static const struct ethtool_ops lan78xx_ethtool_ops = {
	.get_link	= ethtool_op_get_link,
	.nway_reset	= phy_ethtool_nway_reset,
	.get_drvinfo	= lan78xx_get_drvinfo,
	.get_msglevel	= lan78xx_get_msglevel,
	.set_msglevel	= lan78xx_set_msglevel,
	.get_eeprom_len = lan78xx_ethtool_get_eeprom_len,
	.get_eeprom	= lan78xx_ethtool_get_eeprom,
	.set_eeprom	= lan78xx_ethtool_set_eeprom,
	.get_ethtool_stats = lan78xx_get_stats,
	.get_sset_count = lan78xx_get_sset_count,
	.get_strings	= lan78xx_get_strings,
	.get_wol	= lan78xx_get_wol,
	.set_wol	= lan78xx_set_wol,
	.get_ts_info	= ethtool_op_get_ts_info,
	.get_eee	= lan78xx_get_eee,
	.set_eee	= lan78xx_set_eee,
	.get_pauseparam	= lan78xx_get_pause,
	.set_pauseparam	= lan78xx_set_pause,
	.get_link_ksettings = lan78xx_get_link_ksettings,
	.set_link_ksettings = lan78xx_set_link_ksettings,
	.get_regs_len	= lan78xx_get_regs_len,
	.get_regs	= lan78xx_get_regs,
};

static int lan78xx_init_mac_address(struct lan78xx_net *dev)
{
	u32 addr_lo, addr_hi;
	u8 addr[6];
	int ret;

	ret = lan78xx_read_reg(dev, RX_ADDRL, &addr_lo);
	if (ret < 0)
		return ret;

	ret = lan78xx_read_reg(dev, RX_ADDRH, &addr_hi);
	if (ret < 0)
		return ret;

	addr[0] = addr_lo & 0xFF;
	addr[1] = (addr_lo >> 8) & 0xFF;
	addr[2] = (addr_lo >> 16) & 0xFF;
	addr[3] = (addr_lo >> 24) & 0xFF;
	addr[4] = addr_hi & 0xFF;
	addr[5] = (addr_hi >> 8) & 0xFF;

	if (!is_valid_ether_addr(addr)) {
		if (!eth_platform_get_mac_address(&dev->udev->dev, addr)) {
			/* valid address present in Device Tree */
			netif_dbg(dev, ifup, dev->net,
				  "MAC address read from Device Tree");
		} else if (((lan78xx_read_eeprom(dev, EEPROM_MAC_OFFSET,
						 ETH_ALEN, addr) == 0) ||
			    (lan78xx_read_otp(dev, EEPROM_MAC_OFFSET,
					      ETH_ALEN, addr) == 0)) &&
			   is_valid_ether_addr(addr)) {
			/* eeprom values are valid so use them */
			netif_dbg(dev, ifup, dev->net,
				  "MAC address read from EEPROM");
		} else {
			/* generate random MAC */
			eth_random_addr(addr);
			netif_dbg(dev, ifup, dev->net,
				  "MAC address set to random addr");
		}

		addr_lo = addr[0] | (addr[1] << 8) |
			  (addr[2] << 16) | (addr[3] << 24);
		addr_hi = addr[4] | (addr[5] << 8);

		ret = lan78xx_write_reg(dev, RX_ADDRL, addr_lo);
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, RX_ADDRH, addr_hi);
		if (ret < 0)
			return ret;
	}

	ret = lan78xx_write_reg(dev, MAF_LO(0), addr_lo);
	if (ret < 0)
		return ret;

	ret = lan78xx_write_reg(dev, MAF_HI(0), addr_hi | MAF_HI_VALID_);
	if (ret < 0)
		return ret;

	eth_hw_addr_set(dev->net, addr);

	return 0;
}

/* MDIO read and write wrappers for phylib */
static int lan78xx_mdiobus_read(struct mii_bus *bus, int phy_id, int idx)
{
	struct lan78xx_net *dev = bus->priv;
	u32 val, addr;
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->mdiobus_mutex);

	/* confirm MII not busy */
	ret = lan78xx_mdiobus_wait_not_busy(dev);
	if (ret < 0)
		goto done;

	/* set the address, index & direction (read from PHY) */
	addr = mii_access(phy_id, idx, MII_READ);
	ret = lan78xx_write_reg(dev, MII_ACC, addr);
	if (ret < 0)
		goto done;

	ret = lan78xx_mdiobus_wait_not_busy(dev);
	if (ret < 0)
		goto done;

	ret = lan78xx_read_reg(dev, MII_DATA, &val);
	if (ret < 0)
		goto done;

	ret = (int)(val & 0xFFFF);

done:
	mutex_unlock(&dev->mdiobus_mutex);
	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_mdiobus_write(struct mii_bus *bus, int phy_id, int idx,
				 u16 regval)
{
	struct lan78xx_net *dev = bus->priv;
	u32 val, addr;
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->mdiobus_mutex);

	/* confirm MII not busy */
	ret = lan78xx_mdiobus_wait_not_busy(dev);
	if (ret < 0)
		goto done;

	val = (u32)regval;
	ret = lan78xx_write_reg(dev, MII_DATA, val);
	if (ret < 0)
		goto done;

	/* set the address, index & direction (write to PHY) */
	addr = mii_access(phy_id, idx, MII_WRITE);
	ret = lan78xx_write_reg(dev, MII_ACC, addr);
	if (ret < 0)
		goto done;

	ret = lan78xx_mdiobus_wait_not_busy(dev);
	if (ret < 0)
		goto done;

done:
	mutex_unlock(&dev->mdiobus_mutex);
	usb_autopm_put_interface(dev->intf);
	return ret;
}

static int lan78xx_mdio_init(struct lan78xx_net *dev)
{
	struct device_node *node;
	int ret;

	dev->mdiobus = mdiobus_alloc();
	if (!dev->mdiobus) {
		netdev_err(dev->net, "can't allocate MDIO bus\n");
		return -ENOMEM;
	}

	dev->mdiobus->priv = (void *)dev;
	dev->mdiobus->read = lan78xx_mdiobus_read;
	dev->mdiobus->write = lan78xx_mdiobus_write;
	dev->mdiobus->name = "lan78xx-mdiobus";
	dev->mdiobus->parent = &dev->udev->dev;

	snprintf(dev->mdiobus->id, MII_BUS_ID_SIZE, "usb-%03d:%03d",
		 dev->udev->bus->busnum, dev->udev->devnum);

	switch (dev->chipid) {
	case ID_REV_CHIP_ID_7800_:
	case ID_REV_CHIP_ID_7850_:
		/* set to internal PHY id */
		dev->mdiobus->phy_mask = ~(1 << 1);
		break;
	case ID_REV_CHIP_ID_7801_:
		/* scan thru PHYAD[2..0] */
		dev->mdiobus->phy_mask = ~(0xFF);
		break;
	}

	node = of_get_child_by_name(dev->udev->dev.of_node, "mdio");
	ret = of_mdiobus_register(dev->mdiobus, node);
	of_node_put(node);
	if (ret) {
		netdev_err(dev->net, "can't register MDIO bus\n");
		goto exit1;
	}

	netdev_dbg(dev->net, "registered mdiobus bus %s\n", dev->mdiobus->id);
	return 0;
exit1:
	mdiobus_free(dev->mdiobus);
	return ret;
}

static void lan78xx_remove_mdio(struct lan78xx_net *dev)
{
	mdiobus_unregister(dev->mdiobus);
	mdiobus_free(dev->mdiobus);
}

static int irq_map(struct irq_domain *d, unsigned int irq,
		   irq_hw_number_t hwirq)
{
	struct irq_domain_data *data = d->host_data;

	irq_set_chip_data(irq, data);
	irq_set_chip_and_handler(irq, data->irqchip, data->irq_handler);
	irq_set_noprobe(irq);

	return 0;
}

static void irq_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops chip_domain_ops = {
	.map	= irq_map,
	.unmap	= irq_unmap,
};

static void lan78xx_irq_mask(struct irq_data *irqd)
{
	struct irq_domain_data *data = irq_data_get_irq_chip_data(irqd);

	data->irqenable &= ~BIT(irqd_to_hwirq(irqd));
}

static void lan78xx_irq_unmask(struct irq_data *irqd)
{
	struct irq_domain_data *data = irq_data_get_irq_chip_data(irqd);

	data->irqenable |= BIT(irqd_to_hwirq(irqd));
}

static void lan78xx_irq_bus_lock(struct irq_data *irqd)
{
	struct irq_domain_data *data = irq_data_get_irq_chip_data(irqd);

	mutex_lock(&data->irq_lock);
}

static void lan78xx_irq_bus_sync_unlock(struct irq_data *irqd)
{
	struct irq_domain_data *data = irq_data_get_irq_chip_data(irqd);
	struct lan78xx_net *dev =
			container_of(data, struct lan78xx_net, domain_data);
	u32 buf;
	int ret;

	/* call register access here because irq_bus_lock & irq_bus_sync_unlock
	 * are only two callbacks executed in non-atomic contex.
	 */
	ret = lan78xx_read_reg(dev, INT_EP_CTL, &buf);
	if (ret < 0)
		goto irq_bus_sync_unlock;

	if (buf != data->irqenable)
		ret = lan78xx_write_reg(dev, INT_EP_CTL, data->irqenable);

irq_bus_sync_unlock:
	if (ret < 0)
		netdev_err(dev->net, "Failed to sync IRQ enable register: %pe\n",
			   ERR_PTR(ret));

	mutex_unlock(&data->irq_lock);
}

static struct irq_chip lan78xx_irqchip = {
	.name			= "lan78xx-irqs",
	.irq_mask		= lan78xx_irq_mask,
	.irq_unmask		= lan78xx_irq_unmask,
	.irq_bus_lock		= lan78xx_irq_bus_lock,
	.irq_bus_sync_unlock	= lan78xx_irq_bus_sync_unlock,
};

static int lan78xx_setup_irq_domain(struct lan78xx_net *dev)
{
	struct irq_domain *irqdomain;
	unsigned int irqmap = 0;
	u32 buf;
	int ret = 0;

	mutex_init(&dev->domain_data.irq_lock);

	ret = lan78xx_read_reg(dev, INT_EP_CTL, &buf);
	if (ret < 0)
		return ret;

	dev->domain_data.irqenable = buf;

	dev->domain_data.irqchip = &lan78xx_irqchip;
	dev->domain_data.irq_handler = handle_simple_irq;

	irqdomain = irq_domain_create_simple(dev_fwnode(dev->udev->dev.parent), MAX_INT_EP, 0,
					     &chip_domain_ops, &dev->domain_data);
	if (irqdomain) {
		/* create mapping for PHY interrupt */
		irqmap = irq_create_mapping(irqdomain, INT_EP_PHY);
		if (!irqmap) {
			irq_domain_remove(irqdomain);

			irqdomain = NULL;
			ret = -EINVAL;
		}
	} else {
		ret = -EINVAL;
	}

	dev->domain_data.irqdomain = irqdomain;
	dev->domain_data.phyirq = irqmap;

	return ret;
}

static void lan78xx_remove_irq_domain(struct lan78xx_net *dev)
{
	if (dev->domain_data.phyirq > 0) {
		irq_dispose_mapping(dev->domain_data.phyirq);

		if (dev->domain_data.irqdomain)
			irq_domain_remove(dev->domain_data.irqdomain);
	}
	dev->domain_data.phyirq = 0;
	dev->domain_data.irqdomain = NULL;
}

static void lan78xx_mac_config(struct phylink_config *config, unsigned int mode,
			       const struct phylink_link_state *state)
{
	struct net_device *net = to_net_dev(config->dev);
	struct lan78xx_net *dev = netdev_priv(net);
	u32 mac_cr = 0;
	int ret;

	/* Check if the mode is supported */
	if (mode != MLO_AN_FIXED && mode != MLO_AN_PHY) {
		netdev_err(net, "Unsupported negotiation mode: %u\n", mode);
		return;
	}

	switch (state->interface) {
	case PHY_INTERFACE_MODE_GMII:
		mac_cr |= MAC_CR_GMII_EN_;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		break;
	default:
		netdev_warn(net, "Unsupported interface mode: %d\n",
			    state->interface);
		return;
	}

	ret = lan78xx_update_reg(dev, MAC_CR, MAC_CR_GMII_EN_, mac_cr);
	if (ret < 0)
		netdev_err(net, "Failed to config MAC with error %pe\n",
			   ERR_PTR(ret));
}

static void lan78xx_mac_link_down(struct phylink_config *config,
				  unsigned int mode, phy_interface_t interface)
{
	struct net_device *net = to_net_dev(config->dev);
	struct lan78xx_net *dev = netdev_priv(net);
	int ret;

	netif_stop_queue(net);

	/* MAC reset will not de-assert TXEN/RXEN, we need to stop them
	 * manually before reset. TX and RX should be disabled before running
	 * link_up sequence.
	 */
	ret = lan78xx_stop_tx_path(dev);
	if (ret < 0)
		goto link_down_fail;

	ret = lan78xx_stop_rx_path(dev);
	if (ret < 0)
		goto link_down_fail;

	/* MAC reset seems to not affect MAC configuration, no idea if it is
	 * really needed, but it was done in previous driver version. So, leave
	 * it here.
	 */
	ret = lan78xx_mac_reset(dev);
	if (ret < 0)
		goto link_down_fail;

	return;

link_down_fail:
	netdev_err(dev->net, "Failed to set MAC down with error %pe\n",
		   ERR_PTR(ret));
}

/**
 * lan78xx_configure_usb - Configure USB link power settings
 * @dev: pointer to the LAN78xx device structure
 * @speed: negotiated Ethernet link speed (in Mbps)
 *
 * This function configures U1/U2 link power management for SuperSpeed
 * USB devices based on the current Ethernet link speed. It uses the
 * USB_CFG1 register to enable or disable U1 and U2 low-power states.
 *
 * Note: Only LAN7800 and LAN7801 support SuperSpeed (USB 3.x).
 *       LAN7850 is a High-Speed-only (USB 2.0) device and is skipped.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int lan78xx_configure_usb(struct lan78xx_net *dev, int speed)
{
	u32 mask, val;
	int ret;

	/* Only configure USB settings for SuperSpeed devices */
	if (dev->udev->speed != USB_SPEED_SUPER)
		return 0;

	/* LAN7850 does not support USB 3.x */
	if (dev->chipid == ID_REV_CHIP_ID_7850_) {
		netdev_warn_once(dev->net, "Unexpected SuperSpeed for LAN7850 (USB 2.0 only)\n");
		return 0;
	}

	switch (speed) {
	case SPEED_1000:
		/* Disable U2, enable U1 */
		ret = lan78xx_update_reg(dev, USB_CFG1,
					 USB_CFG1_DEV_U2_INIT_EN_, 0);
		if (ret < 0)
			return ret;

		return lan78xx_update_reg(dev, USB_CFG1,
					  USB_CFG1_DEV_U1_INIT_EN_,
					  USB_CFG1_DEV_U1_INIT_EN_);

	case SPEED_100:
	case SPEED_10:
		/* Enable both U1 and U2 */
		mask = USB_CFG1_DEV_U1_INIT_EN_ | USB_CFG1_DEV_U2_INIT_EN_;
		val = mask;
		return lan78xx_update_reg(dev, USB_CFG1, mask, val);

	default:
		netdev_warn(dev->net, "Unsupported link speed: %d\n", speed);
		return -EINVAL;
	}
}

/**
 * lan78xx_configure_flowcontrol - Set MAC and FIFO flow control configuration
 * @dev: pointer to the LAN78xx device structure
 * @tx_pause: enable transmission of pause frames
 * @rx_pause: enable reception of pause frames
 *
 * This function configures the LAN78xx flow control settings by writing
 * to the FLOW and FCT_FLOW registers. The pause time is set to the
 * maximum allowed value (65535 quanta). FIFO thresholds are selected
 * based on USB speed.
 *
 * The Pause Time field is measured in units of 512-bit times (quanta):
 *   - At 1 Gbps: 1 quanta = 512 ns → max ~33.6 ms pause
 *   - At 100 Mbps: 1 quanta = 5.12 µs → max ~335 ms pause
 *   - At 10 Mbps: 1 quanta = 51.2 µs → max ~3.3 s pause
 *
 * Flow control thresholds (FCT_FLOW) are used to trigger pause/resume:
 *   - RXUSED is the number of bytes used in the RX FIFO
 *   - Flow is turned ON when RXUSED ≥ FLOW_ON threshold
 *   - Flow is turned OFF when RXUSED ≤ FLOW_OFF threshold
 *   - Both thresholds are encoded in units of 512 bytes (rounded up)
 *
 * Thresholds differ by USB speed because available USB bandwidth
 * affects how fast packets can be drained from the RX FIFO:
 *   - USB 3.x (SuperSpeed):
 *       FLOW_ON  = 9216 bytes → 18 units
 *       FLOW_OFF = 4096 bytes →  8 units
 *   - USB 2.0 (High-Speed):
 *       FLOW_ON  = 8704 bytes → 17 units
 *       FLOW_OFF = 1024 bytes →  2 units
 *
 * Note: The FCT_FLOW register must be configured before enabling TX pause
 *       (i.e., before setting FLOW_CR_TX_FCEN_), as required by the hardware.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int lan78xx_configure_flowcontrol(struct lan78xx_net *dev,
					 bool tx_pause, bool rx_pause)
{
	/* Use maximum pause time: 65535 quanta (512-bit times) */
	const u32 pause_time_quanta = 65535;
	u32 fct_flow = 0;
	u32 flow = 0;
	int ret;

	/* Prepare MAC flow control bits */
	if (tx_pause)
		flow |= FLOW_CR_TX_FCEN_ | pause_time_quanta;

	if (rx_pause)
		flow |= FLOW_CR_RX_FCEN_;

	/* Select RX FIFO thresholds based on USB speed
	 *
	 * FCT_FLOW layout:
	 *   bits [6:0]   FLOW_ON threshold (RXUSED ≥ ON → assert pause)
	 *   bits [14:8]  FLOW_OFF threshold (RXUSED ≤ OFF → deassert pause)
	 *   thresholds are expressed in units of 512 bytes
	 */
	switch (dev->udev->speed) {
	case USB_SPEED_SUPER:
		fct_flow = FLOW_CTRL_THRESHOLD(FLOW_ON_SS, FLOW_OFF_SS);
		break;
	case USB_SPEED_HIGH:
		fct_flow = FLOW_CTRL_THRESHOLD(FLOW_ON_HS, FLOW_OFF_HS);
		break;
	default:
		netdev_warn(dev->net, "Unsupported USB speed: %d\n",
			    dev->udev->speed);
		return -EINVAL;
	}

	/* Step 1: Write FIFO thresholds before enabling pause frames */
	ret = lan78xx_write_reg(dev, FCT_FLOW, fct_flow);
	if (ret < 0)
		return ret;

	/* Step 2: Enable MAC pause functionality */
	return lan78xx_write_reg(dev, FLOW, flow);
}

static void lan78xx_mac_link_up(struct phylink_config *config,
				struct phy_device *phy,
				unsigned int mode, phy_interface_t interface,
				int speed, int duplex,
				bool tx_pause, bool rx_pause)
{
	struct net_device *net = to_net_dev(config->dev);
	struct lan78xx_net *dev = netdev_priv(net);
	u32 mac_cr = 0;
	int ret;

	switch (speed) {
	case SPEED_1000:
		mac_cr |= MAC_CR_SPEED_1000_;
		break;
	case SPEED_100:
		mac_cr |= MAC_CR_SPEED_100_;
		break;
	case SPEED_10:
		mac_cr |= MAC_CR_SPEED_10_;
		break;
	default:
		netdev_err(dev->net, "Unsupported speed %d\n", speed);
		return;
	}

	if (duplex == DUPLEX_FULL)
		mac_cr |= MAC_CR_FULL_DUPLEX_;

	/* make sure TXEN and RXEN are disabled before reconfiguring MAC */
	ret = lan78xx_update_reg(dev, MAC_CR, MAC_CR_SPEED_MASK_ |
				 MAC_CR_FULL_DUPLEX_ | MAC_CR_EEE_EN_, mac_cr);
	if (ret < 0)
		goto link_up_fail;

	ret = lan78xx_configure_flowcontrol(dev, tx_pause, rx_pause);
	if (ret < 0)
		goto link_up_fail;

	ret = lan78xx_configure_usb(dev, speed);
	if (ret < 0)
		goto link_up_fail;

	lan78xx_rx_urb_submit_all(dev);

	ret = lan78xx_flush_rx_fifo(dev);
	if (ret < 0)
		goto link_up_fail;

	ret = lan78xx_flush_tx_fifo(dev);
	if (ret < 0)
		goto link_up_fail;

	ret = lan78xx_start_tx_path(dev);
	if (ret < 0)
		goto link_up_fail;

	ret = lan78xx_start_rx_path(dev);
	if (ret < 0)
		goto link_up_fail;

	netif_start_queue(net);

	return;

link_up_fail:
	netdev_err(dev->net, "Failed to set MAC up with error %pe\n",
		   ERR_PTR(ret));
}

/**
 * lan78xx_mac_eee_enable - Enable or disable MAC-side EEE support
 * @dev: LAN78xx device
 * @enable: true to enable EEE, false to disable
 *
 * This function sets or clears the MAC_CR_EEE_EN_ bit to control Energy
 * Efficient Ethernet (EEE) operation. According to current understanding
 * of the LAN7800 documentation, this bit can be modified while TX and RX
 * are enabled. No explicit requirement was found to disable data paths
 * before changing this bit.
 *
 * Return: 0 on success or a negative error code
 */
static int lan78xx_mac_eee_enable(struct lan78xx_net *dev, bool enable)
{
	u32 mac_cr = 0;

	if (enable)
		mac_cr |= MAC_CR_EEE_EN_;

	return lan78xx_update_reg(dev, MAC_CR, MAC_CR_EEE_EN_, mac_cr);
}

static void lan78xx_mac_disable_tx_lpi(struct phylink_config *config)
{
	struct net_device *net = to_net_dev(config->dev);
	struct lan78xx_net *dev = netdev_priv(net);

	lan78xx_mac_eee_enable(dev, false);
}

static int lan78xx_mac_enable_tx_lpi(struct phylink_config *config, u32 timer,
				     bool tx_clk_stop)
{
	struct net_device *net = to_net_dev(config->dev);
	struct lan78xx_net *dev = netdev_priv(net);
	int ret;

	/* Software should only change this field when Energy Efficient
	 * Ethernet Enable (EEEEN) is cleared. We ensure that by clearing
	 * EEEEN during probe, and phylink itself guarantees that
	 * mac_disable_tx_lpi() will have been previously called.
	 */
	ret = lan78xx_write_reg(dev, EEE_TX_LPI_REQ_DLY, timer);
	if (ret < 0)
		return ret;

	return lan78xx_mac_eee_enable(dev, true);
}

static const struct phylink_mac_ops lan78xx_phylink_mac_ops = {
	.mac_config = lan78xx_mac_config,
	.mac_link_down = lan78xx_mac_link_down,
	.mac_link_up = lan78xx_mac_link_up,
	.mac_disable_tx_lpi = lan78xx_mac_disable_tx_lpi,
	.mac_enable_tx_lpi = lan78xx_mac_enable_tx_lpi,
};

/**
 * lan78xx_set_fixed_link() - Set fixed link configuration for LAN7801
 * @dev: LAN78xx device
 *
 * Use fixed link configuration with 1 Gbps full duplex. This is used in special
 * cases like EVB-KSZ9897-1, where LAN7801 acts as a USB-to-Ethernet interface
 * to a switch without a visible PHY.
 *
 * Return: pointer to the registered fixed PHY, or ERR_PTR() on error.
 */
static int lan78xx_set_fixed_link(struct lan78xx_net *dev)
{
	static const struct phylink_link_state state = {
		.speed = SPEED_1000,
		.duplex = DUPLEX_FULL,
	};

	netdev_info(dev->net,
		    "No PHY found on LAN7801 – using fixed link instead (e.g. EVB-KSZ9897-1)\n");

	return phylink_set_fixed_link(dev->phylink, &state);
}

/**
 * lan78xx_get_phy() - Probe or register PHY device and set interface mode
 * @dev: LAN78xx device structure
 *
 * This function attempts to find a PHY on the MDIO bus. If no PHY is found
 * and the chip is LAN7801, it registers a fixed PHY as fallback. It also
 * sets dev->interface based on chip ID and detected PHY type.
 *
 * Return: a valid PHY device pointer, or ERR_PTR() on failure.
 */
static struct phy_device *lan78xx_get_phy(struct lan78xx_net *dev)
{
	struct phy_device *phydev;

	/* Attempt to locate a PHY on the MDIO bus */
	phydev = phy_find_first(dev->mdiobus);

	switch (dev->chipid) {
	case ID_REV_CHIP_ID_7801_:
		if (phydev) {
			/* External RGMII PHY detected */
			dev->interface = PHY_INTERFACE_MODE_RGMII_ID;
			phydev->is_internal = false;

			if (!phydev->drv)
				netdev_warn(dev->net,
					    "PHY driver not found – assuming RGMII delays are on PCB or strapped for the PHY\n");

			return phydev;
		}

		dev->interface = PHY_INTERFACE_MODE_RGMII;
		/* No PHY found – fallback to fixed PHY (e.g. KSZ switch board) */
		return NULL;

	case ID_REV_CHIP_ID_7800_:
	case ID_REV_CHIP_ID_7850_:
		if (!phydev)
			return ERR_PTR(-ENODEV);

		/* These use internal GMII-connected PHY */
		dev->interface = PHY_INTERFACE_MODE_GMII;
		phydev->is_internal = true;
		return phydev;

	default:
		netdev_err(dev->net, "Unknown CHIP ID: 0x%08x\n", dev->chipid);
		return ERR_PTR(-ENODEV);
	}
}

/**
 * lan78xx_mac_prepare_for_phy() - Preconfigure MAC-side interface settings
 * @dev: LAN78xx device
 *
 * Configure MAC-side registers according to dev->interface, which should be
 * set by lan78xx_get_phy().
 *
 * - For PHY_INTERFACE_MODE_RGMII:
 *   Enable MAC-side TXC delay. This mode seems to be used in a special setup
 *   without a real PHY, likely on EVB-KSZ9897-1. In that design, LAN7801 is
 *   connected to the KSZ9897 switch, and the link timing is expected to be
 *   hardwired (e.g. via strapping or board layout). No devicetree support is
 *   assumed here.
 *
 * - For PHY_INTERFACE_MODE_RGMII_ID:
 *   Disable MAC-side delay and rely on the PHY driver to provide delay.
 *
 * - For GMII, no MAC-specific config is needed.
 *
 * Return: 0 on success or a negative error code.
 */
static int lan78xx_mac_prepare_for_phy(struct lan78xx_net *dev)
{
	int ret;

	switch (dev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		/* Enable MAC-side TX clock delay */
		ret = lan78xx_write_reg(dev, MAC_RGMII_ID,
					MAC_RGMII_ID_TXC_DELAY_EN_);
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, RGMII_TX_BYP_DLL, 0x3D00);
		if (ret < 0)
			return ret;

		ret = lan78xx_update_reg(dev, HW_CFG,
					 HW_CFG_CLK125_EN_ | HW_CFG_REFCLK25_EN_,
					 HW_CFG_CLK125_EN_ | HW_CFG_REFCLK25_EN_);
		if (ret < 0)
			return ret;

		break;

	case PHY_INTERFACE_MODE_RGMII_ID:
		/* Disable MAC-side TXC delay, PHY provides it */
		ret = lan78xx_write_reg(dev, MAC_RGMII_ID, 0);
		if (ret < 0)
			return ret;

		break;

	case PHY_INTERFACE_MODE_GMII:
		/* No MAC-specific configuration required */
		break;

	default:
		netdev_warn(dev->net, "Unsupported interface mode: %d\n",
			    dev->interface);
		break;
	}

	return 0;
}

/**
 * lan78xx_configure_leds_from_dt() - Configure LED enables based on DT
 * @dev: LAN78xx device
 * @phydev: PHY device (must be valid)
 *
 * Reads "microchip,led-modes" property from the PHY's DT node and enables
 * the corresponding number of LEDs by writing to HW_CFG.
 *
 * This helper preserves the original logic, enabling up to 4 LEDs.
 * If the property is not present, this function does nothing.
 *
 * Return: 0 on success or a negative error code.
 */
static int lan78xx_configure_leds_from_dt(struct lan78xx_net *dev,
					  struct phy_device *phydev)
{
	struct device_node *np = phydev->mdio.dev.of_node;
	u32 reg;
	int len, ret;

	if (!np)
		return 0;

	len = of_property_count_elems_of_size(np, "microchip,led-modes",
					      sizeof(u32));
	if (len < 0)
		return 0;

	ret = lan78xx_read_reg(dev, HW_CFG, &reg);
	if (ret < 0)
		return ret;

	reg &= ~(HW_CFG_LED0_EN_ | HW_CFG_LED1_EN_ |
		 HW_CFG_LED2_EN_ | HW_CFG_LED3_EN_);

	reg |= (len > 0) * HW_CFG_LED0_EN_ |
	       (len > 1) * HW_CFG_LED1_EN_ |
	       (len > 2) * HW_CFG_LED2_EN_ |
	       (len > 3) * HW_CFG_LED3_EN_;

	return lan78xx_write_reg(dev, HW_CFG, reg);
}

static int lan78xx_phylink_setup(struct lan78xx_net *dev)
{
	struct phylink_config *pc = &dev->phylink_config;
	struct phylink *phylink;

	pc->dev = &dev->net->dev;
	pc->type = PHYLINK_NETDEV;
	pc->mac_capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE | MAC_10 |
			       MAC_100 | MAC_1000FD;
	pc->mac_managed_pm = true;
	pc->lpi_capabilities = MAC_100FD | MAC_1000FD;
	/*
	 * Default TX LPI (Low Power Idle) request delay count is set to 50us.
	 *
	 * Source: LAN7800 Documentation, DS00001992H, Section 15.1.57, Page 204.
	 *
	 * Reasoning:
	 * According to the application note in the LAN7800 documentation, a
	 * zero delay may negatively impact the TX data path’s ability to
	 * support Gigabit operation. A value of 50us is recommended as a
	 * reasonable default when the part operates at Gigabit speeds,
	 * balancing stability and power efficiency in EEE mode. This delay can
	 * be increased based on performance testing, as EEE is designed for
	 * scenarios with mostly idle links and occasional bursts of full
	 * bandwidth transmission. The goal is to ensure reliable Gigabit
	 * performance without overly aggressive power optimization during
	 * inactive periods.
	 */
	pc->lpi_timer_default = 50;
	pc->eee_enabled_default = true;

	if (dev->chipid == ID_REV_CHIP_ID_7801_)
		phy_interface_set_rgmii(pc->supported_interfaces);
	else
		__set_bit(PHY_INTERFACE_MODE_GMII, pc->supported_interfaces);

	memcpy(dev->phylink_config.lpi_interfaces,
	       dev->phylink_config.supported_interfaces,
	       sizeof(dev->phylink_config.lpi_interfaces));

	phylink = phylink_create(pc, dev->net->dev.fwnode,
				 dev->interface, &lan78xx_phylink_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	dev->phylink = phylink;

	return 0;
}

static void lan78xx_phy_uninit(struct lan78xx_net *dev)
{
	if (dev->phylink) {
		phylink_disconnect_phy(dev->phylink);
		phylink_destroy(dev->phylink);
		dev->phylink = NULL;
	}
}

static int lan78xx_phy_init(struct lan78xx_net *dev)
{
	struct phy_device *phydev;
	int ret;

	phydev = lan78xx_get_phy(dev);
	/* phydev can be NULL if no PHY is found and the chip is LAN7801,
	 * which will use a fixed link later.
	 * If an  error occurs, return the error code immediately.
	 */
	if (IS_ERR(phydev))
		return PTR_ERR(phydev);

	ret = lan78xx_phylink_setup(dev);
	if (ret < 0)
		return ret;

	ret = lan78xx_mac_prepare_for_phy(dev);
	if (ret < 0)
		goto phylink_uninit;

	/* If no PHY is found, set up a fixed link. It is very specific to
	 * the LAN7801 and is used in special cases like EVB-KSZ9897-1 where
	 * LAN7801 acts as a USB-to-Ethernet interface to a switch without
	 * a visible PHY.
	 */
	if (!phydev) {
		ret = lan78xx_set_fixed_link(dev);
		if (ret < 0)
			goto phylink_uninit;

		/* No PHY found, so set up a fixed link and return early.
		 * No need to configure PHY IRQ or attach to phylink.
		 */
		return 0;
	}

	/* if phyirq is not set, use polling mode in phylib */
	if (dev->domain_data.phyirq > 0)
		phydev->irq = dev->domain_data.phyirq;
	else
		phydev->irq = PHY_POLL;
	netdev_dbg(dev->net, "phydev->irq = %d\n", phydev->irq);

	ret = phylink_connect_phy(dev->phylink, phydev);
	if (ret) {
		netdev_err(dev->net, "can't attach PHY to %s, error %pe\n",
			   dev->mdiobus->id, ERR_PTR(ret));
		goto phylink_uninit;
	}

	ret = lan78xx_configure_leds_from_dt(dev, phydev);
	if (ret < 0)
		goto phylink_uninit;

	return 0;

phylink_uninit:
	lan78xx_phy_uninit(dev);

	return ret;
}

static int lan78xx_set_rx_max_frame_length(struct lan78xx_net *dev, int size)
{
	bool rxenabled;
	u32 buf;
	int ret;

	ret = lan78xx_read_reg(dev, MAC_RX, &buf);
	if (ret < 0)
		return ret;

	rxenabled = ((buf & MAC_RX_RXEN_) != 0);

	if (rxenabled) {
		buf &= ~MAC_RX_RXEN_;
		ret = lan78xx_write_reg(dev, MAC_RX, buf);
		if (ret < 0)
			return ret;
	}

	/* add 4 to size for FCS */
	buf &= ~MAC_RX_MAX_SIZE_MASK_;
	buf |= (((size + 4) << MAC_RX_MAX_SIZE_SHIFT_) & MAC_RX_MAX_SIZE_MASK_);

	ret = lan78xx_write_reg(dev, MAC_RX, buf);
	if (ret < 0)
		return ret;

	if (rxenabled) {
		buf |= MAC_RX_RXEN_;
		ret = lan78xx_write_reg(dev, MAC_RX, buf);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int unlink_urbs(struct lan78xx_net *dev, struct sk_buff_head *q)
{
	struct sk_buff *skb;
	unsigned long flags;
	int count = 0;

	spin_lock_irqsave(&q->lock, flags);
	while (!skb_queue_empty(q)) {
		struct skb_data	*entry;
		struct urb *urb;
		int ret;

		skb_queue_walk(q, skb) {
			entry = (struct skb_data *)skb->cb;
			if (entry->state != unlink_start)
				goto found;
		}
		break;
found:
		entry->state = unlink_start;
		urb = entry->urb;

		/* Get reference count of the URB to avoid it to be
		 * freed during usb_unlink_urb, which may trigger
		 * use-after-free problem inside usb_unlink_urb since
		 * usb_unlink_urb is always racing with .complete
		 * handler(include defer_bh).
		 */
		usb_get_urb(urb);
		spin_unlock_irqrestore(&q->lock, flags);
		/* during some PM-driven resume scenarios,
		 * these (async) unlinks complete immediately
		 */
		ret = usb_unlink_urb(urb);
		if (ret != -EINPROGRESS && ret != 0)
			netdev_dbg(dev->net, "unlink urb err, %d\n", ret);
		else
			count++;
		usb_put_urb(urb);
		spin_lock_irqsave(&q->lock, flags);
	}
	spin_unlock_irqrestore(&q->lock, flags);
	return count;
}

static int lan78xx_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	int max_frame_len = RX_MAX_FRAME_LEN(new_mtu);
	int ret;

	/* no second zero-length packet read wanted after mtu-sized packets */
	if ((max_frame_len % dev->maxpacket) == 0)
		return -EDOM;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	ret = lan78xx_set_rx_max_frame_length(dev, max_frame_len);
	if (ret < 0)
		netdev_err(dev->net, "MTU changed to %d from %d failed with %pe\n",
			   new_mtu, netdev->mtu, ERR_PTR(ret));
	else
		WRITE_ONCE(netdev->mtu, new_mtu);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_set_mac_addr(struct net_device *netdev, void *p)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct sockaddr *addr = p;
	u32 addr_lo, addr_hi;
	int ret;

	if (netif_running(netdev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(netdev, addr->sa_data);

	addr_lo = netdev->dev_addr[0] |
		  netdev->dev_addr[1] << 8 |
		  netdev->dev_addr[2] << 16 |
		  netdev->dev_addr[3] << 24;
	addr_hi = netdev->dev_addr[4] |
		  netdev->dev_addr[5] << 8;

	ret = lan78xx_write_reg(dev, RX_ADDRL, addr_lo);
	if (ret < 0)
		return ret;

	ret = lan78xx_write_reg(dev, RX_ADDRH, addr_hi);
	if (ret < 0)
		return ret;

	/* Added to support MAC address changes */
	ret = lan78xx_write_reg(dev, MAF_LO(0), addr_lo);
	if (ret < 0)
		return ret;

	return lan78xx_write_reg(dev, MAF_HI(0), addr_hi | MAF_HI_VALID_);
}

/* Enable or disable Rx checksum offload engine */
static int lan78xx_set_features(struct net_device *netdev,
				netdev_features_t features)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	unsigned long flags;

	spin_lock_irqsave(&pdata->rfe_ctl_lock, flags);

	if (features & NETIF_F_RXCSUM) {
		pdata->rfe_ctl |= RFE_CTL_TCPUDP_COE_ | RFE_CTL_IP_COE_;
		pdata->rfe_ctl |= RFE_CTL_ICMP_COE_ | RFE_CTL_IGMP_COE_;
	} else {
		pdata->rfe_ctl &= ~(RFE_CTL_TCPUDP_COE_ | RFE_CTL_IP_COE_);
		pdata->rfe_ctl &= ~(RFE_CTL_ICMP_COE_ | RFE_CTL_IGMP_COE_);
	}

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		pdata->rfe_ctl |= RFE_CTL_VLAN_STRIP_;
	else
		pdata->rfe_ctl &= ~RFE_CTL_VLAN_STRIP_;

	if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
		pdata->rfe_ctl |= RFE_CTL_VLAN_FILTER_;
	else
		pdata->rfe_ctl &= ~RFE_CTL_VLAN_FILTER_;

	spin_unlock_irqrestore(&pdata->rfe_ctl_lock, flags);

	return lan78xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);
}

static void lan78xx_deferred_vlan_write(struct work_struct *param)
{
	struct lan78xx_priv *pdata =
			container_of(param, struct lan78xx_priv, set_vlan);
	struct lan78xx_net *dev = pdata->dev;

	lan78xx_dataport_write(dev, DP_SEL_RSEL_VLAN_DA_, 0,
			       DP_SEL_VHF_VLAN_LEN, pdata->vlan_table);
}

static int lan78xx_vlan_rx_add_vid(struct net_device *netdev,
				   __be16 proto, u16 vid)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	u16 vid_bit_index;
	u16 vid_dword_index;

	vid_dword_index = (vid >> 5) & 0x7F;
	vid_bit_index = vid & 0x1F;

	pdata->vlan_table[vid_dword_index] |= (1 << vid_bit_index);

	/* defer register writes to a sleepable context */
	schedule_work(&pdata->set_vlan);

	return 0;
}

static int lan78xx_vlan_rx_kill_vid(struct net_device *netdev,
				    __be16 proto, u16 vid)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	u16 vid_bit_index;
	u16 vid_dword_index;

	vid_dword_index = (vid >> 5) & 0x7F;
	vid_bit_index = vid & 0x1F;

	pdata->vlan_table[vid_dword_index] &= ~(1 << vid_bit_index);

	/* defer register writes to a sleepable context */
	schedule_work(&pdata->set_vlan);

	return 0;
}

static int lan78xx_init_ltm(struct lan78xx_net *dev)
{
	u32 regs[6] = { 0 };
	int ret;
	u32 buf;

	ret = lan78xx_read_reg(dev, USB_CFG1, &buf);
	if (ret < 0)
		goto init_ltm_failed;

	if (buf & USB_CFG1_LTM_ENABLE_) {
		u8 temp[2];
		/* Get values from EEPROM first */
		if (lan78xx_read_eeprom(dev, 0x3F, 2, temp) == 0) {
			if (temp[0] == 24) {
				ret = lan78xx_read_raw_eeprom(dev,
							      temp[1] * 2,
							      24,
							      (u8 *)regs);
				if (ret < 0)
					return ret;
			}
		} else if (lan78xx_read_otp(dev, 0x3F, 2, temp) == 0) {
			if (temp[0] == 24) {
				ret = lan78xx_read_raw_otp(dev,
							   temp[1] * 2,
							   24,
							   (u8 *)regs);
				if (ret < 0)
					return ret;
			}
		}
	}

	ret = lan78xx_write_reg(dev, LTM_BELT_IDLE0, regs[0]);
	if (ret < 0)
		goto init_ltm_failed;

	ret = lan78xx_write_reg(dev, LTM_BELT_IDLE1, regs[1]);
	if (ret < 0)
		goto init_ltm_failed;

	ret = lan78xx_write_reg(dev, LTM_BELT_ACT0, regs[2]);
	if (ret < 0)
		goto init_ltm_failed;

	ret = lan78xx_write_reg(dev, LTM_BELT_ACT1, regs[3]);
	if (ret < 0)
		goto init_ltm_failed;

	ret = lan78xx_write_reg(dev, LTM_INACTIVE0, regs[4]);
	if (ret < 0)
		goto init_ltm_failed;

	ret = lan78xx_write_reg(dev, LTM_INACTIVE1, regs[5]);
	if (ret < 0)
		goto init_ltm_failed;

	return 0;

init_ltm_failed:
	netdev_err(dev->net, "Failed to init LTM with error %pe\n", ERR_PTR(ret));
	return ret;
}

static int lan78xx_urb_config_init(struct lan78xx_net *dev)
{
	int result = 0;

	switch (dev->udev->speed) {
	case USB_SPEED_SUPER:
		dev->rx_urb_size = RX_SS_URB_SIZE;
		dev->tx_urb_size = TX_SS_URB_SIZE;
		dev->n_rx_urbs = RX_SS_URB_NUM;
		dev->n_tx_urbs = TX_SS_URB_NUM;
		dev->bulk_in_delay = SS_BULK_IN_DELAY;
		dev->burst_cap = SS_BURST_CAP_SIZE / SS_USB_PKT_SIZE;
		break;
	case USB_SPEED_HIGH:
		dev->rx_urb_size = RX_HS_URB_SIZE;
		dev->tx_urb_size = TX_HS_URB_SIZE;
		dev->n_rx_urbs = RX_HS_URB_NUM;
		dev->n_tx_urbs = TX_HS_URB_NUM;
		dev->bulk_in_delay = HS_BULK_IN_DELAY;
		dev->burst_cap = HS_BURST_CAP_SIZE / HS_USB_PKT_SIZE;
		break;
	case USB_SPEED_FULL:
		dev->rx_urb_size = RX_FS_URB_SIZE;
		dev->tx_urb_size = TX_FS_URB_SIZE;
		dev->n_rx_urbs = RX_FS_URB_NUM;
		dev->n_tx_urbs = TX_FS_URB_NUM;
		dev->bulk_in_delay = FS_BULK_IN_DELAY;
		dev->burst_cap = FS_BURST_CAP_SIZE / FS_USB_PKT_SIZE;
		break;
	default:
		netdev_warn(dev->net, "USB bus speed not supported\n");
		result = -EIO;
		break;
	}

	return result;
}

static int lan78xx_reset(struct lan78xx_net *dev)
{
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	unsigned long timeout;
	int ret;
	u32 buf;

	ret = lan78xx_read_reg(dev, HW_CFG, &buf);
	if (ret < 0)
		return ret;

	buf |= HW_CFG_LRST_;

	ret = lan78xx_write_reg(dev, HW_CFG, buf);
	if (ret < 0)
		return ret;

	timeout = jiffies + HZ;
	do {
		mdelay(1);
		ret = lan78xx_read_reg(dev, HW_CFG, &buf);
		if (ret < 0)
			return ret;

		if (time_after(jiffies, timeout)) {
			netdev_warn(dev->net,
				    "timeout on completion of LiteReset");
			ret = -ETIMEDOUT;
			return ret;
		}
	} while (buf & HW_CFG_LRST_);

	ret = lan78xx_init_mac_address(dev);
	if (ret < 0)
		return ret;

	/* save DEVID for later usage */
	ret = lan78xx_read_reg(dev, ID_REV, &buf);
	if (ret < 0)
		return ret;

	dev->chipid = (buf & ID_REV_CHIP_ID_MASK_) >> 16;
	dev->chiprev = buf & ID_REV_CHIP_REV_MASK_;

	/* Respond to the IN token with a NAK */
	ret = lan78xx_read_reg(dev, USB_CFG0, &buf);
	if (ret < 0)
		return ret;

	buf |= USB_CFG_BIR_;

	ret = lan78xx_write_reg(dev, USB_CFG0, buf);
	if (ret < 0)
		return ret;

	/* Init LTM */
	ret = lan78xx_init_ltm(dev);
	if (ret < 0)
		return ret;

	ret = lan78xx_write_reg(dev, BURST_CAP, dev->burst_cap);
	if (ret < 0)
		return ret;

	ret = lan78xx_write_reg(dev, BULK_IN_DLY, dev->bulk_in_delay);
	if (ret < 0)
		return ret;

	ret = lan78xx_read_reg(dev, HW_CFG, &buf);
	if (ret < 0)
		return ret;

	buf |= HW_CFG_MEF_;
	buf |= HW_CFG_CLK125_EN_;
	buf |= HW_CFG_REFCLK25_EN_;

	ret = lan78xx_write_reg(dev, HW_CFG, buf);
	if (ret < 0)
		return ret;

	ret = lan78xx_read_reg(dev, USB_CFG0, &buf);
	if (ret < 0)
		return ret;

	buf |= USB_CFG_BCE_;

	ret = lan78xx_write_reg(dev, USB_CFG0, buf);
	if (ret < 0)
		return ret;

	/* set FIFO sizes */
	buf = (MAX_RX_FIFO_SIZE - 512) / 512;

	ret = lan78xx_write_reg(dev, FCT_RX_FIFO_END, buf);
	if (ret < 0)
		return ret;

	buf = (MAX_TX_FIFO_SIZE - 512) / 512;

	ret = lan78xx_write_reg(dev, FCT_TX_FIFO_END, buf);
	if (ret < 0)
		return ret;

	ret = lan78xx_write_reg(dev, INT_STS, INT_STS_CLEAR_ALL_);
	if (ret < 0)
		return ret;

	ret = lan78xx_write_reg(dev, FLOW, 0);
	if (ret < 0)
		return ret;

	ret = lan78xx_write_reg(dev, FCT_FLOW, 0);
	if (ret < 0)
		return ret;

	/* Don't need rfe_ctl_lock during initialisation */
	ret = lan78xx_read_reg(dev, RFE_CTL, &pdata->rfe_ctl);
	if (ret < 0)
		return ret;

	pdata->rfe_ctl |= RFE_CTL_BCAST_EN_ | RFE_CTL_DA_PERFECT_;

	ret = lan78xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);
	if (ret < 0)
		return ret;

	/* Enable or disable checksum offload engines */
	ret = lan78xx_set_features(dev->net, dev->net->features);
	if (ret < 0)
		return ret;

	lan78xx_set_multicast(dev->net);

	/* reset PHY */
	ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
	if (ret < 0)
		return ret;

	buf |= PMT_CTL_PHY_RST_;

	ret = lan78xx_write_reg(dev, PMT_CTL, buf);
	if (ret < 0)
		return ret;

	timeout = jiffies + HZ;
	do {
		mdelay(1);
		ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
		if (ret < 0)
			return ret;

		if (time_after(jiffies, timeout)) {
			netdev_warn(dev->net, "timeout waiting for PHY Reset");
			ret = -ETIMEDOUT;
			return ret;
		}
	} while ((buf & PMT_CTL_PHY_RST_) || !(buf & PMT_CTL_READY_));

	ret = lan78xx_read_reg(dev, MAC_CR, &buf);
	if (ret < 0)
		return ret;

	buf &= ~(MAC_CR_AUTO_DUPLEX_ | MAC_CR_AUTO_SPEED_ | MAC_CR_EEE_EN_);

	/* LAN7801 only has RGMII mode */
	if (dev->chipid == ID_REV_CHIP_ID_7801_)
		buf &= ~MAC_CR_GMII_EN_;

	ret = lan78xx_write_reg(dev, MAC_CR, buf);
	if (ret < 0)
		return ret;

	ret = lan78xx_set_rx_max_frame_length(dev,
					      RX_MAX_FRAME_LEN(dev->net->mtu));

	return ret;
}

static void lan78xx_init_stats(struct lan78xx_net *dev)
{
	u32 *p;
	int i;

	/* initialize for stats update
	 * some counters are 20bits and some are 32bits
	 */
	p = (u32 *)&dev->stats.rollover_max;
	for (i = 0; i < (sizeof(dev->stats.rollover_max) / (sizeof(u32))); i++)
		p[i] = 0xFFFFF;

	dev->stats.rollover_max.rx_unicast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.rx_broadcast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.rx_multicast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.eee_rx_lpi_transitions = 0xFFFFFFFF;
	dev->stats.rollover_max.eee_rx_lpi_time = 0xFFFFFFFF;
	dev->stats.rollover_max.tx_unicast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.tx_broadcast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.tx_multicast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.eee_tx_lpi_transitions = 0xFFFFFFFF;
	dev->stats.rollover_max.eee_tx_lpi_time = 0xFFFFFFFF;

	set_bit(EVENT_STAT_UPDATE, &dev->flags);
}

static int lan78xx_open(struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);
	int ret;

	netif_dbg(dev, ifup, dev->net, "open device");

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->dev_mutex);

	lan78xx_init_stats(dev);

	napi_enable(&dev->napi);

	set_bit(EVENT_DEV_OPEN, &dev->flags);

	/* for Link Check */
	if (dev->urb_intr) {
		ret = usb_submit_urb(dev->urb_intr, GFP_KERNEL);
		if (ret < 0) {
			netif_err(dev, ifup, dev->net,
				  "intr submit %d\n", ret);
			goto done;
		}
	}

	phylink_start(dev->phylink);

done:
	mutex_unlock(&dev->dev_mutex);

	if (ret < 0)
		usb_autopm_put_interface(dev->intf);

	return ret;
}

static void lan78xx_terminate_urbs(struct lan78xx_net *dev)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(unlink_wakeup);
	DECLARE_WAITQUEUE(wait, current);
	int temp;

	/* ensure there are no more active urbs */
	add_wait_queue(&unlink_wakeup, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	dev->wait = &unlink_wakeup;
	temp = unlink_urbs(dev, &dev->txq) + unlink_urbs(dev, &dev->rxq);

	/* maybe wait for deletions to finish. */
	while (!skb_queue_empty(&dev->rxq) ||
	       !skb_queue_empty(&dev->txq)) {
		schedule_timeout(msecs_to_jiffies(UNLINK_TIMEOUT_MS));
		set_current_state(TASK_UNINTERRUPTIBLE);
		netif_dbg(dev, ifdown, dev->net,
			  "waited for %d urb completions", temp);
	}
	set_current_state(TASK_RUNNING);
	dev->wait = NULL;
	remove_wait_queue(&unlink_wakeup, &wait);

	/* empty Rx done, Rx overflow and Tx pend queues
	 */
	while (!skb_queue_empty(&dev->rxq_done)) {
		struct sk_buff *skb = skb_dequeue(&dev->rxq_done);

		lan78xx_release_rx_buf(dev, skb);
	}

	skb_queue_purge(&dev->rxq_overflow);
	skb_queue_purge(&dev->txq_pend);
}

static int lan78xx_stop(struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);

	netif_dbg(dev, ifup, dev->net, "stop device");

	mutex_lock(&dev->dev_mutex);

	if (timer_pending(&dev->stat_monitor))
		timer_delete_sync(&dev->stat_monitor);

	clear_bit(EVENT_DEV_OPEN, &dev->flags);
	napi_disable(&dev->napi);

	lan78xx_terminate_urbs(dev);

	netif_info(dev, ifdown, dev->net,
		   "stop stats: rx/tx %lu/%lu, errs %lu/%lu\n",
		   net->stats.rx_packets, net->stats.tx_packets,
		   net->stats.rx_errors, net->stats.tx_errors);

	phylink_stop(dev->phylink);

	usb_kill_urb(dev->urb_intr);

	/* deferred work (task, timer, softirq) must also stop.
	 * can't flush_scheduled_work() until we drop rtnl (later),
	 * else workers could deadlock; so make workers a NOP.
	 */
	clear_bit(EVENT_TX_HALT, &dev->flags);
	clear_bit(EVENT_RX_HALT, &dev->flags);
	clear_bit(EVENT_PHY_INT_ACK, &dev->flags);
	clear_bit(EVENT_STAT_UPDATE, &dev->flags);

	cancel_delayed_work_sync(&dev->wq);

	usb_autopm_put_interface(dev->intf);

	mutex_unlock(&dev->dev_mutex);

	return 0;
}

static enum skb_state defer_bh(struct lan78xx_net *dev, struct sk_buff *skb,
			       struct sk_buff_head *list, enum skb_state state)
{
	unsigned long flags;
	enum skb_state old_state;
	struct skb_data *entry = (struct skb_data *)skb->cb;

	spin_lock_irqsave(&list->lock, flags);
	old_state = entry->state;
	entry->state = state;

	__skb_unlink(skb, list);
	spin_unlock(&list->lock);
	spin_lock(&dev->rxq_done.lock);

	__skb_queue_tail(&dev->rxq_done, skb);
	if (skb_queue_len(&dev->rxq_done) == 1)
		napi_schedule(&dev->napi);

	spin_unlock_irqrestore(&dev->rxq_done.lock, flags);

	return old_state;
}

static void tx_complete(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct skb_data *entry = (struct skb_data *)skb->cb;
	struct lan78xx_net *dev = entry->dev;

	if (urb->status == 0) {
		dev->net->stats.tx_packets += entry->num_of_packet;
		dev->net->stats.tx_bytes += entry->length;
	} else {
		dev->net->stats.tx_errors += entry->num_of_packet;

		switch (urb->status) {
		case -EPIPE:
			lan78xx_defer_kevent(dev, EVENT_TX_HALT);
			break;

		/* software-driven interface shutdown */
		case -ECONNRESET:
		case -ESHUTDOWN:
			netif_dbg(dev, tx_err, dev->net,
				  "tx err interface gone %d\n",
				  entry->urb->status);
			break;

		case -EPROTO:
		case -ETIME:
		case -EILSEQ:
			netif_stop_queue(dev->net);
			netif_dbg(dev, tx_err, dev->net,
				  "tx err queue stopped %d\n",
				  entry->urb->status);
			break;
		default:
			netif_dbg(dev, tx_err, dev->net,
				  "unknown tx err %d\n",
				  entry->urb->status);
			break;
		}
	}

	usb_autopm_put_interface_async(dev->intf);

	skb_unlink(skb, &dev->txq);

	lan78xx_release_tx_buf(dev, skb);

	/* Re-schedule NAPI if Tx data pending but no URBs in progress.
	 */
	if (skb_queue_empty(&dev->txq) &&
	    !skb_queue_empty(&dev->txq_pend))
		napi_schedule(&dev->napi);
}

static void lan78xx_queue_skb(struct sk_buff_head *list,
			      struct sk_buff *newsk, enum skb_state state)
{
	struct skb_data *entry = (struct skb_data *)newsk->cb;

	__skb_queue_tail(list, newsk);
	entry->state = state;
}

static unsigned int lan78xx_tx_urb_space(struct lan78xx_net *dev)
{
	return skb_queue_len(&dev->txq_free) * dev->tx_urb_size;
}

static unsigned int lan78xx_tx_pend_data_len(struct lan78xx_net *dev)
{
	return dev->tx_pend_data_len;
}

static void lan78xx_tx_pend_skb_add(struct lan78xx_net *dev,
				    struct sk_buff *skb,
				    unsigned int *tx_pend_data_len)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->txq_pend.lock, flags);

	__skb_queue_tail(&dev->txq_pend, skb);

	dev->tx_pend_data_len += skb->len;
	*tx_pend_data_len = dev->tx_pend_data_len;

	spin_unlock_irqrestore(&dev->txq_pend.lock, flags);
}

static void lan78xx_tx_pend_skb_head_add(struct lan78xx_net *dev,
					 struct sk_buff *skb,
					 unsigned int *tx_pend_data_len)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->txq_pend.lock, flags);

	__skb_queue_head(&dev->txq_pend, skb);

	dev->tx_pend_data_len += skb->len;
	*tx_pend_data_len = dev->tx_pend_data_len;

	spin_unlock_irqrestore(&dev->txq_pend.lock, flags);
}

static void lan78xx_tx_pend_skb_get(struct lan78xx_net *dev,
				    struct sk_buff **skb,
				    unsigned int *tx_pend_data_len)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->txq_pend.lock, flags);

	*skb = __skb_dequeue(&dev->txq_pend);
	if (*skb)
		dev->tx_pend_data_len -= (*skb)->len;
	*tx_pend_data_len = dev->tx_pend_data_len;

	spin_unlock_irqrestore(&dev->txq_pend.lock, flags);
}

static netdev_tx_t
lan78xx_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);
	unsigned int tx_pend_data_len;

	if (test_bit(EVENT_DEV_ASLEEP, &dev->flags))
		schedule_delayed_work(&dev->wq, 0);

	skb_tx_timestamp(skb);

	lan78xx_tx_pend_skb_add(dev, skb, &tx_pend_data_len);

	/* Set up a Tx URB if none is in progress */

	if (skb_queue_empty(&dev->txq))
		napi_schedule(&dev->napi);

	/* Stop stack Tx queue if we have enough data to fill
	 * all the free Tx URBs.
	 */
	if (tx_pend_data_len > lan78xx_tx_urb_space(dev)) {
		netif_stop_queue(net);

		netif_dbg(dev, hw, dev->net, "tx data len: %u, urb space %u",
			  tx_pend_data_len, lan78xx_tx_urb_space(dev));

		/* Kick off transmission of pending data */

		if (!skb_queue_empty(&dev->txq_free))
			napi_schedule(&dev->napi);
	}

	return NETDEV_TX_OK;
}

static int lan78xx_bind(struct lan78xx_net *dev, struct usb_interface *intf)
{
	struct lan78xx_priv *pdata = NULL;
	int ret;
	int i;

	dev->data[0] = (unsigned long)kzalloc(sizeof(*pdata), GFP_KERNEL);

	pdata = (struct lan78xx_priv *)(dev->data[0]);
	if (!pdata) {
		netdev_warn(dev->net, "Unable to allocate lan78xx_priv");
		return -ENOMEM;
	}

	pdata->dev = dev;

	spin_lock_init(&pdata->rfe_ctl_lock);
	mutex_init(&pdata->dataport_mutex);

	INIT_WORK(&pdata->set_multicast, lan78xx_deferred_multicast_write);

	for (i = 0; i < DP_SEL_VHF_VLAN_LEN; i++)
		pdata->vlan_table[i] = 0;

	INIT_WORK(&pdata->set_vlan, lan78xx_deferred_vlan_write);

	dev->net->features = 0;

	if (DEFAULT_TX_CSUM_ENABLE)
		dev->net->features |= NETIF_F_HW_CSUM;

	if (DEFAULT_RX_CSUM_ENABLE)
		dev->net->features |= NETIF_F_RXCSUM;

	if (DEFAULT_TSO_CSUM_ENABLE)
		dev->net->features |= NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_SG;

	if (DEFAULT_VLAN_RX_OFFLOAD)
		dev->net->features |= NETIF_F_HW_VLAN_CTAG_RX;

	if (DEFAULT_VLAN_FILTER_ENABLE)
		dev->net->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	dev->net->hw_features = dev->net->features;

	ret = lan78xx_setup_irq_domain(dev);
	if (ret < 0) {
		netdev_warn(dev->net,
			    "lan78xx_setup_irq_domain() failed : %d", ret);
		goto out1;
	}

	/* Init all registers */
	ret = lan78xx_reset(dev);
	if (ret) {
		netdev_warn(dev->net, "Registers INIT FAILED....");
		goto out2;
	}

	ret = lan78xx_mdio_init(dev);
	if (ret) {
		netdev_warn(dev->net, "MDIO INIT FAILED.....");
		goto out2;
	}

	dev->net->flags |= IFF_MULTICAST;

	pdata->wol = WAKE_MAGIC;

	return ret;

out2:
	lan78xx_remove_irq_domain(dev);

out1:
	netdev_warn(dev->net, "Bind routine FAILED");
	cancel_work_sync(&pdata->set_multicast);
	cancel_work_sync(&pdata->set_vlan);
	kfree(pdata);
	return ret;
}

static void lan78xx_unbind(struct lan78xx_net *dev, struct usb_interface *intf)
{
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);

	lan78xx_remove_irq_domain(dev);

	lan78xx_remove_mdio(dev);

	if (pdata) {
		cancel_work_sync(&pdata->set_multicast);
		cancel_work_sync(&pdata->set_vlan);
		netif_dbg(dev, ifdown, dev->net, "free pdata");
		kfree(pdata);
		pdata = NULL;
		dev->data[0] = 0;
	}
}

static void lan78xx_rx_csum_offload(struct lan78xx_net *dev,
				    struct sk_buff *skb,
				    u32 rx_cmd_a, u32 rx_cmd_b)
{
	/* HW Checksum offload appears to be flawed if used when not stripping
	 * VLAN headers. Drop back to S/W checksums under these conditions.
	 */
	if (!(dev->net->features & NETIF_F_RXCSUM) ||
	    unlikely(rx_cmd_a & RX_CMD_A_ICSM_) ||
	    ((rx_cmd_a & RX_CMD_A_FVTG_) &&
	     !(dev->net->features & NETIF_F_HW_VLAN_CTAG_RX))) {
		skb->ip_summed = CHECKSUM_NONE;
	} else {
		skb->csum = ntohs((u16)(rx_cmd_b >> RX_CMD_B_CSUM_SHIFT_));
		skb->ip_summed = CHECKSUM_COMPLETE;
	}
}

static void lan78xx_rx_vlan_offload(struct lan78xx_net *dev,
				    struct sk_buff *skb,
				    u32 rx_cmd_a, u32 rx_cmd_b)
{
	if ((dev->net->features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    (rx_cmd_a & RX_CMD_A_FVTG_))
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       (rx_cmd_b & 0xffff));
}

static void lan78xx_skb_return(struct lan78xx_net *dev, struct sk_buff *skb)
{
	dev->net->stats.rx_packets++;
	dev->net->stats.rx_bytes += skb->len;

	skb->protocol = eth_type_trans(skb, dev->net);

	netif_dbg(dev, rx_status, dev->net, "< rx, len %zu, type 0x%x\n",
		  skb->len + sizeof(struct ethhdr), skb->protocol);
	memset(skb->cb, 0, sizeof(struct skb_data));

	if (skb_defer_rx_timestamp(skb))
		return;

	napi_gro_receive(&dev->napi, skb);
}

static int lan78xx_rx(struct lan78xx_net *dev, struct sk_buff *skb,
		      int budget, int *work_done)
{
	if (skb->len < RX_SKB_MIN_LEN)
		return 0;

	/* Extract frames from the URB buffer and pass each one to
	 * the stack in a new NAPI SKB.
	 */
	while (skb->len > 0) {
		u32 rx_cmd_a, rx_cmd_b, align_count, size;
		u16 rx_cmd_c;
		unsigned char *packet;

		rx_cmd_a = get_unaligned_le32(skb->data);
		skb_pull(skb, sizeof(rx_cmd_a));

		rx_cmd_b = get_unaligned_le32(skb->data);
		skb_pull(skb, sizeof(rx_cmd_b));

		rx_cmd_c = get_unaligned_le16(skb->data);
		skb_pull(skb, sizeof(rx_cmd_c));

		packet = skb->data;

		/* get the packet length */
		size = (rx_cmd_a & RX_CMD_A_LEN_MASK_);
		align_count = (4 - ((size + RXW_PADDING) % 4)) % 4;

		if (unlikely(size > skb->len)) {
			netif_dbg(dev, rx_err, dev->net,
				  "size err rx_cmd_a=0x%08x\n",
				  rx_cmd_a);
			return 0;
		}

		if (unlikely(rx_cmd_a & RX_CMD_A_RED_)) {
			netif_dbg(dev, rx_err, dev->net,
				  "Error rx_cmd_a=0x%08x", rx_cmd_a);
		} else {
			u32 frame_len;
			struct sk_buff *skb2;

			if (unlikely(size < ETH_FCS_LEN)) {
				netif_dbg(dev, rx_err, dev->net,
					  "size err rx_cmd_a=0x%08x\n",
					  rx_cmd_a);
				return 0;
			}

			frame_len = size - ETH_FCS_LEN;

			skb2 = napi_alloc_skb(&dev->napi, frame_len);
			if (!skb2)
				return 0;

			memcpy(skb2->data, packet, frame_len);

			skb_put(skb2, frame_len);

			lan78xx_rx_csum_offload(dev, skb2, rx_cmd_a, rx_cmd_b);
			lan78xx_rx_vlan_offload(dev, skb2, rx_cmd_a, rx_cmd_b);

			/* Processing of the URB buffer must complete once
			 * it has started. If the NAPI work budget is exhausted
			 * while frames remain they are added to the overflow
			 * queue for delivery in the next NAPI polling cycle.
			 */
			if (*work_done < budget) {
				lan78xx_skb_return(dev, skb2);
				++(*work_done);
			} else {
				skb_queue_tail(&dev->rxq_overflow, skb2);
			}
		}

		skb_pull(skb, size);

		/* skip padding bytes before the next frame starts */
		if (skb->len)
			skb_pull(skb, align_count);
	}

	return 1;
}

static inline void rx_process(struct lan78xx_net *dev, struct sk_buff *skb,
			      int budget, int *work_done)
{
	if (!lan78xx_rx(dev, skb, budget, work_done)) {
		netif_dbg(dev, rx_err, dev->net, "drop\n");
		dev->net->stats.rx_errors++;
	}
}

static void rx_complete(struct urb *urb)
{
	struct sk_buff	*skb = (struct sk_buff *)urb->context;
	struct skb_data	*entry = (struct skb_data *)skb->cb;
	struct lan78xx_net *dev = entry->dev;
	int urb_status = urb->status;
	enum skb_state state;

	netif_dbg(dev, rx_status, dev->net,
		  "rx done: status %d", urb->status);

	skb_put(skb, urb->actual_length);
	state = rx_done;

	if (urb != entry->urb)
		netif_warn(dev, rx_err, dev->net, "URB pointer mismatch");

	switch (urb_status) {
	case 0:
		if (skb->len < RX_SKB_MIN_LEN) {
			state = rx_cleanup;
			dev->net->stats.rx_errors++;
			dev->net->stats.rx_length_errors++;
			netif_dbg(dev, rx_err, dev->net,
				  "rx length %d\n", skb->len);
		}
		usb_mark_last_busy(dev->udev);
		break;
	case -EPIPE:
		dev->net->stats.rx_errors++;
		lan78xx_defer_kevent(dev, EVENT_RX_HALT);
		fallthrough;
	case -ECONNRESET:				/* async unlink */
	case -ESHUTDOWN:				/* hardware gone */
		netif_dbg(dev, ifdown, dev->net,
			  "rx shutdown, code %d\n", urb_status);
		state = rx_cleanup;
		break;
	case -EPROTO:
	case -ETIME:
	case -EILSEQ:
		dev->net->stats.rx_errors++;
		state = rx_cleanup;
		break;

	/* data overrun ... flush fifo? */
	case -EOVERFLOW:
		dev->net->stats.rx_over_errors++;
		fallthrough;

	default:
		state = rx_cleanup;
		dev->net->stats.rx_errors++;
		netif_dbg(dev, rx_err, dev->net, "rx status %d\n", urb_status);
		break;
	}

	state = defer_bh(dev, skb, &dev->rxq, state);
}

static int rx_submit(struct lan78xx_net *dev, struct sk_buff *skb, gfp_t flags)
{
	struct skb_data	*entry = (struct skb_data *)skb->cb;
	size_t size = dev->rx_urb_size;
	struct urb *urb = entry->urb;
	unsigned long lockflags;
	int ret = 0;

	usb_fill_bulk_urb(urb, dev->udev, dev->pipe_in,
			  skb->data, size, rx_complete, skb);

	spin_lock_irqsave(&dev->rxq.lock, lockflags);

	if (netif_device_present(dev->net) &&
	    netif_running(dev->net) &&
	    !test_bit(EVENT_RX_HALT, &dev->flags) &&
	    !test_bit(EVENT_DEV_ASLEEP, &dev->flags)) {
		ret = usb_submit_urb(urb, flags);
		switch (ret) {
		case 0:
			lan78xx_queue_skb(&dev->rxq, skb, rx_start);
			break;
		case -EPIPE:
			lan78xx_defer_kevent(dev, EVENT_RX_HALT);
			break;
		case -ENODEV:
		case -ENOENT:
			netif_dbg(dev, ifdown, dev->net, "device gone\n");
			netif_device_detach(dev->net);
			break;
		case -EHOSTUNREACH:
			ret = -ENOLINK;
			napi_schedule(&dev->napi);
			break;
		default:
			netif_dbg(dev, rx_err, dev->net,
				  "rx submit, %d\n", ret);
			napi_schedule(&dev->napi);
			break;
		}
	} else {
		netif_dbg(dev, ifdown, dev->net, "rx: stopped\n");
		ret = -ENOLINK;
	}
	spin_unlock_irqrestore(&dev->rxq.lock, lockflags);

	if (ret)
		lan78xx_release_rx_buf(dev, skb);

	return ret;
}

static void lan78xx_rx_urb_submit_all(struct lan78xx_net *dev)
{
	struct sk_buff *rx_buf;

	/* Ensure the maximum number of Rx URBs is submitted
	 */
	while ((rx_buf = lan78xx_get_rx_buf(dev)) != NULL) {
		if (rx_submit(dev, rx_buf, GFP_ATOMIC) != 0)
			break;
	}
}

static void lan78xx_rx_urb_resubmit(struct lan78xx_net *dev,
				    struct sk_buff *rx_buf)
{
	/* reset SKB data pointers */

	rx_buf->data = rx_buf->head;
	skb_reset_tail_pointer(rx_buf);
	rx_buf->len = 0;
	rx_buf->data_len = 0;

	rx_submit(dev, rx_buf, GFP_ATOMIC);
}

static void lan78xx_fill_tx_cmd_words(struct sk_buff *skb, u8 *buffer)
{
	u32 tx_cmd_a;
	u32 tx_cmd_b;

	tx_cmd_a = (u32)(skb->len & TX_CMD_A_LEN_MASK_) | TX_CMD_A_FCS_;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		tx_cmd_a |= TX_CMD_A_IPE_ | TX_CMD_A_TPE_;

	tx_cmd_b = 0;
	if (skb_is_gso(skb)) {
		u16 mss = max(skb_shinfo(skb)->gso_size, TX_CMD_B_MSS_MIN_);

		tx_cmd_b = (mss << TX_CMD_B_MSS_SHIFT_) & TX_CMD_B_MSS_MASK_;

		tx_cmd_a |= TX_CMD_A_LSO_;
	}

	if (skb_vlan_tag_present(skb)) {
		tx_cmd_a |= TX_CMD_A_IVTG_;
		tx_cmd_b |= skb_vlan_tag_get(skb) & TX_CMD_B_VTAG_MASK_;
	}

	put_unaligned_le32(tx_cmd_a, buffer);
	put_unaligned_le32(tx_cmd_b, buffer + 4);
}

static struct skb_data *lan78xx_tx_buf_fill(struct lan78xx_net *dev,
					    struct sk_buff *tx_buf)
{
	struct skb_data *entry = (struct skb_data *)tx_buf->cb;
	int remain = dev->tx_urb_size;
	u8 *tx_data = tx_buf->data;
	u32 urb_len = 0;

	entry->num_of_packet = 0;
	entry->length = 0;

	/* Work through the pending SKBs and copy the data of each SKB into
	 * the URB buffer if there room for all the SKB data.
	 *
	 * There must be at least DST+SRC+TYPE in the SKB (with padding enabled)
	 */
	while (remain >= TX_SKB_MIN_LEN) {
		unsigned int pending_bytes;
		unsigned int align_bytes;
		struct sk_buff *skb;
		unsigned int len;

		lan78xx_tx_pend_skb_get(dev, &skb, &pending_bytes);

		if (!skb)
			break;

		align_bytes = (TX_ALIGNMENT - (urb_len % TX_ALIGNMENT)) %
			      TX_ALIGNMENT;
		len = align_bytes + TX_CMD_LEN + skb->len;
		if (len > remain) {
			lan78xx_tx_pend_skb_head_add(dev, skb, &pending_bytes);
			break;
		}

		tx_data += align_bytes;

		lan78xx_fill_tx_cmd_words(skb, tx_data);
		tx_data += TX_CMD_LEN;

		len = skb->len;
		if (skb_copy_bits(skb, 0, tx_data, len) < 0) {
			struct net_device_stats *stats = &dev->net->stats;

			stats->tx_dropped++;
			dev_kfree_skb_any(skb);
			tx_data -= TX_CMD_LEN;
			continue;
		}

		tx_data += len;
		entry->length += len;
		entry->num_of_packet += skb_shinfo(skb)->gso_segs ?: 1;

		dev_kfree_skb_any(skb);

		urb_len = (u32)(tx_data - (u8 *)tx_buf->data);

		remain = dev->tx_urb_size - urb_len;
	}

	skb_put(tx_buf, urb_len);

	return entry;
}

static void lan78xx_tx_bh(struct lan78xx_net *dev)
{
	int ret;

	/* Start the stack Tx queue if it was stopped
	 */
	netif_tx_lock(dev->net);
	if (netif_queue_stopped(dev->net)) {
		if (lan78xx_tx_pend_data_len(dev) < lan78xx_tx_urb_space(dev))
			netif_wake_queue(dev->net);
	}
	netif_tx_unlock(dev->net);

	/* Go through the Tx pending queue and set up URBs to transfer
	 * the data to the device. Stop if no more pending data or URBs,
	 * or if an error occurs when a URB is submitted.
	 */
	do {
		struct skb_data *entry;
		struct sk_buff *tx_buf;
		unsigned long flags;

		if (skb_queue_empty(&dev->txq_pend))
			break;

		tx_buf = lan78xx_get_tx_buf(dev);
		if (!tx_buf)
			break;

		entry = lan78xx_tx_buf_fill(dev, tx_buf);

		spin_lock_irqsave(&dev->txq.lock, flags);
		ret = usb_autopm_get_interface_async(dev->intf);
		if (ret < 0) {
			spin_unlock_irqrestore(&dev->txq.lock, flags);
			goto out;
		}

		usb_fill_bulk_urb(entry->urb, dev->udev, dev->pipe_out,
				  tx_buf->data, tx_buf->len, tx_complete,
				  tx_buf);

		if (tx_buf->len % dev->maxpacket == 0) {
			/* send USB_ZERO_PACKET */
			entry->urb->transfer_flags |= URB_ZERO_PACKET;
		}

#ifdef CONFIG_PM
		/* if device is asleep stop outgoing packet processing */
		if (test_bit(EVENT_DEV_ASLEEP, &dev->flags)) {
			usb_anchor_urb(entry->urb, &dev->deferred);
			netif_stop_queue(dev->net);
			spin_unlock_irqrestore(&dev->txq.lock, flags);
			netdev_dbg(dev->net,
				   "Delaying transmission for resumption\n");
			return;
		}
#endif
		ret = usb_submit_urb(entry->urb, GFP_ATOMIC);
		switch (ret) {
		case 0:
			netif_trans_update(dev->net);
			lan78xx_queue_skb(&dev->txq, tx_buf, tx_start);
			break;
		case -EPIPE:
			netif_stop_queue(dev->net);
			lan78xx_defer_kevent(dev, EVENT_TX_HALT);
			usb_autopm_put_interface_async(dev->intf);
			break;
		case -ENODEV:
		case -ENOENT:
			netif_dbg(dev, tx_err, dev->net,
				  "tx submit urb err %d (disconnected?)", ret);
			netif_device_detach(dev->net);
			break;
		default:
			usb_autopm_put_interface_async(dev->intf);
			netif_dbg(dev, tx_err, dev->net,
				  "tx submit urb err %d\n", ret);
			break;
		}

		spin_unlock_irqrestore(&dev->txq.lock, flags);

		if (ret) {
			netdev_warn(dev->net, "failed to tx urb %d\n", ret);
out:
			dev->net->stats.tx_dropped += entry->num_of_packet;
			lan78xx_release_tx_buf(dev, tx_buf);
		}
	} while (ret == 0);
}

static int lan78xx_bh(struct lan78xx_net *dev, int budget)
{
	struct sk_buff_head done;
	struct sk_buff *rx_buf;
	struct skb_data *entry;
	unsigned long flags;
	int work_done = 0;

	/* Pass frames received in the last NAPI cycle before
	 * working on newly completed URBs.
	 */
	while (!skb_queue_empty(&dev->rxq_overflow)) {
		lan78xx_skb_return(dev, skb_dequeue(&dev->rxq_overflow));
		++work_done;
	}

	/* Take a snapshot of the done queue and move items to a
	 * temporary queue. Rx URB completions will continue to add
	 * to the done queue.
	 */
	__skb_queue_head_init(&done);

	spin_lock_irqsave(&dev->rxq_done.lock, flags);
	skb_queue_splice_init(&dev->rxq_done, &done);
	spin_unlock_irqrestore(&dev->rxq_done.lock, flags);

	/* Extract receive frames from completed URBs and
	 * pass them to the stack. Re-submit each completed URB.
	 */
	while ((work_done < budget) &&
	       (rx_buf = __skb_dequeue(&done))) {
		entry = (struct skb_data *)(rx_buf->cb);
		switch (entry->state) {
		case rx_done:
			rx_process(dev, rx_buf, budget, &work_done);
			break;
		case rx_cleanup:
			break;
		default:
			netdev_dbg(dev->net, "rx buf state %d\n",
				   entry->state);
			break;
		}

		lan78xx_rx_urb_resubmit(dev, rx_buf);
	}

	/* If budget was consumed before processing all the URBs put them
	 * back on the front of the done queue. They will be first to be
	 * processed in the next NAPI cycle.
	 */
	spin_lock_irqsave(&dev->rxq_done.lock, flags);
	skb_queue_splice(&done, &dev->rxq_done);
	spin_unlock_irqrestore(&dev->rxq_done.lock, flags);

	if (netif_device_present(dev->net) && netif_running(dev->net)) {
		/* reset update timer delta */
		if (timer_pending(&dev->stat_monitor) && (dev->delta != 1)) {
			dev->delta = 1;
			mod_timer(&dev->stat_monitor,
				  jiffies + STAT_UPDATE_TIMER);
		}

		/* Submit all free Rx URBs */

		if (!test_bit(EVENT_RX_HALT, &dev->flags))
			lan78xx_rx_urb_submit_all(dev);

		/* Submit new Tx URBs */

		lan78xx_tx_bh(dev);
	}

	return work_done;
}

static int lan78xx_poll(struct napi_struct *napi, int budget)
{
	struct lan78xx_net *dev = container_of(napi, struct lan78xx_net, napi);
	int result = budget;
	int work_done;

	/* Don't do any work if the device is suspended */

	if (test_bit(EVENT_DEV_ASLEEP, &dev->flags)) {
		napi_complete_done(napi, 0);
		return 0;
	}

	/* Process completed URBs and submit new URBs */

	work_done = lan78xx_bh(dev, budget);

	if (work_done < budget) {
		napi_complete_done(napi, work_done);

		/* Start a new polling cycle if data was received or
		 * data is waiting to be transmitted.
		 */
		if (!skb_queue_empty(&dev->rxq_done)) {
			napi_schedule(napi);
		} else if (netif_carrier_ok(dev->net)) {
			if (skb_queue_empty(&dev->txq) &&
			    !skb_queue_empty(&dev->txq_pend)) {
				napi_schedule(napi);
			} else {
				netif_tx_lock(dev->net);
				if (netif_queue_stopped(dev->net)) {
					netif_wake_queue(dev->net);
					napi_schedule(napi);
				}
				netif_tx_unlock(dev->net);
			}
		}
		result = work_done;
	}

	return result;
}

static void lan78xx_delayedwork(struct work_struct *work)
{
	int status;
	struct lan78xx_net *dev;

	dev = container_of(work, struct lan78xx_net, wq.work);

	if (test_bit(EVENT_DEV_DISCONNECT, &dev->flags))
		return;

	if (usb_autopm_get_interface(dev->intf) < 0)
		return;

	if (test_bit(EVENT_TX_HALT, &dev->flags)) {
		unlink_urbs(dev, &dev->txq);

		status = usb_clear_halt(dev->udev, dev->pipe_out);
		if (status < 0 &&
		    status != -EPIPE &&
		    status != -ESHUTDOWN) {
			if (netif_msg_tx_err(dev))
				netdev_err(dev->net,
					   "can't clear tx halt, status %d\n",
					   status);
		} else {
			clear_bit(EVENT_TX_HALT, &dev->flags);
			if (status != -ESHUTDOWN)
				netif_wake_queue(dev->net);
		}
	}

	if (test_bit(EVENT_RX_HALT, &dev->flags)) {
		unlink_urbs(dev, &dev->rxq);
		status = usb_clear_halt(dev->udev, dev->pipe_in);
		if (status < 0 &&
		    status != -EPIPE &&
		    status != -ESHUTDOWN) {
			if (netif_msg_rx_err(dev))
				netdev_err(dev->net,
					   "can't clear rx halt, status %d\n",
					   status);
		} else {
			clear_bit(EVENT_RX_HALT, &dev->flags);
			napi_schedule(&dev->napi);
		}
	}

	if (test_bit(EVENT_PHY_INT_ACK, &dev->flags)) {
		int ret = 0;

		clear_bit(EVENT_PHY_INT_ACK, &dev->flags);
		ret = lan78xx_phy_int_ack(dev);
		if (ret)
			netdev_info(dev->net, "PHY INT ack failed (%pe)\n",
				    ERR_PTR(ret));
	}

	if (test_bit(EVENT_STAT_UPDATE, &dev->flags)) {
		lan78xx_update_stats(dev);

		clear_bit(EVENT_STAT_UPDATE, &dev->flags);

		mod_timer(&dev->stat_monitor,
			  jiffies + (STAT_UPDATE_TIMER * dev->delta));

		dev->delta = min((dev->delta * 2), 50);
	}

	usb_autopm_put_interface(dev->intf);
}

static void intr_complete(struct urb *urb)
{
	struct lan78xx_net *dev = urb->context;
	int status = urb->status;

	switch (status) {
	/* success */
	case 0:
		lan78xx_status(dev, urb);
		break;

	/* software-driven interface shutdown */
	case -ENOENT:			/* urb killed */
	case -ENODEV:			/* hardware gone */
	case -ESHUTDOWN:		/* hardware gone */
		netif_dbg(dev, ifdown, dev->net,
			  "intr shutdown, code %d\n", status);
		return;

	/* NOTE:  not throttling like RX/TX, since this endpoint
	 * already polls infrequently
	 */
	default:
		netdev_dbg(dev->net, "intr status %d\n", status);
		break;
	}

	if (!netif_device_present(dev->net) ||
	    !netif_running(dev->net)) {
		netdev_warn(dev->net, "not submitting new status URB");
		return;
	}

	memset(urb->transfer_buffer, 0, urb->transfer_buffer_length);
	status = usb_submit_urb(urb, GFP_ATOMIC);

	switch (status) {
	case  0:
		break;
	case -ENODEV:
	case -ENOENT:
		netif_dbg(dev, timer, dev->net,
			  "intr resubmit %d (disconnect?)", status);
		netif_device_detach(dev->net);
		break;
	default:
		netif_err(dev, timer, dev->net,
			  "intr resubmit --> %d\n", status);
		break;
	}
}

static void lan78xx_disconnect(struct usb_interface *intf)
{
	struct lan78xx_net *dev;
	struct usb_device *udev;
	struct net_device *net;

	dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
	if (!dev)
		return;

	udev = interface_to_usbdev(intf);
	net = dev->net;

	rtnl_lock();
	phylink_stop(dev->phylink);
	phylink_disconnect_phy(dev->phylink);
	rtnl_unlock();

	netif_napi_del(&dev->napi);

	unregister_netdev(net);

	timer_shutdown_sync(&dev->stat_monitor);
	set_bit(EVENT_DEV_DISCONNECT, &dev->flags);
	cancel_delayed_work_sync(&dev->wq);

	phylink_destroy(dev->phylink);

	usb_scuttle_anchored_urbs(&dev->deferred);

	lan78xx_unbind(dev, intf);

	lan78xx_free_tx_resources(dev);
	lan78xx_free_rx_resources(dev);

	usb_kill_urb(dev->urb_intr);
	usb_free_urb(dev->urb_intr);

	free_netdev(net);
	usb_put_dev(udev);
}

static void lan78xx_tx_timeout(struct net_device *net, unsigned int txqueue)
{
	struct lan78xx_net *dev = netdev_priv(net);

	unlink_urbs(dev, &dev->txq);
	napi_schedule(&dev->napi);
}

static netdev_features_t lan78xx_features_check(struct sk_buff *skb,
						struct net_device *netdev,
						netdev_features_t features)
{
	struct lan78xx_net *dev = netdev_priv(netdev);

	if (skb->len > LAN78XX_TSO_SIZE(dev))
		features &= ~NETIF_F_GSO_MASK;

	features = vlan_features_check(skb, features);
	features = vxlan_features_check(skb, features);

	return features;
}

static const struct net_device_ops lan78xx_netdev_ops = {
	.ndo_open		= lan78xx_open,
	.ndo_stop		= lan78xx_stop,
	.ndo_start_xmit		= lan78xx_start_xmit,
	.ndo_tx_timeout		= lan78xx_tx_timeout,
	.ndo_change_mtu		= lan78xx_change_mtu,
	.ndo_set_mac_address	= lan78xx_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
	.ndo_set_rx_mode	= lan78xx_set_multicast,
	.ndo_set_features	= lan78xx_set_features,
	.ndo_vlan_rx_add_vid	= lan78xx_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= lan78xx_vlan_rx_kill_vid,
	.ndo_features_check	= lan78xx_features_check,
};

static void lan78xx_stat_monitor(struct timer_list *t)
{
	struct lan78xx_net *dev = timer_container_of(dev, t, stat_monitor);

	lan78xx_defer_kevent(dev, EVENT_STAT_UPDATE);
}

static int lan78xx_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_host_endpoint *ep_blkin, *ep_blkout, *ep_intr;
	struct lan78xx_net *dev;
	struct net_device *netdev;
	struct usb_device *udev;
	int ret;
	unsigned int maxp;
	unsigned int period;
	u8 *buf = NULL;

	udev = interface_to_usbdev(intf);
	udev = usb_get_dev(udev);

	netdev = alloc_etherdev(sizeof(struct lan78xx_net));
	if (!netdev) {
		dev_err(&intf->dev, "Error: OOM\n");
		ret = -ENOMEM;
		goto out1;
	}

	SET_NETDEV_DEV(netdev, &intf->dev);

	dev = netdev_priv(netdev);
	dev->udev = udev;
	dev->intf = intf;
	dev->net = netdev;
	dev->msg_enable = netif_msg_init(msg_level, NETIF_MSG_DRV
					| NETIF_MSG_PROBE | NETIF_MSG_LINK);

	skb_queue_head_init(&dev->rxq);
	skb_queue_head_init(&dev->txq);
	skb_queue_head_init(&dev->rxq_done);
	skb_queue_head_init(&dev->txq_pend);
	skb_queue_head_init(&dev->rxq_overflow);
	mutex_init(&dev->mdiobus_mutex);
	mutex_init(&dev->dev_mutex);

	ret = lan78xx_urb_config_init(dev);
	if (ret < 0)
		goto out2;

	ret = lan78xx_alloc_tx_resources(dev);
	if (ret < 0)
		goto out2;

	ret = lan78xx_alloc_rx_resources(dev);
	if (ret < 0)
		goto out3;

	/* MTU range: 68 - 9000 */
	netdev->max_mtu = MAX_SINGLE_PACKET_SIZE;

	netif_set_tso_max_size(netdev, LAN78XX_TSO_SIZE(dev));

	netif_napi_add(netdev, &dev->napi, lan78xx_poll);

	INIT_DELAYED_WORK(&dev->wq, lan78xx_delayedwork);
	init_usb_anchor(&dev->deferred);

	netdev->netdev_ops = &lan78xx_netdev_ops;
	netdev->watchdog_timeo = TX_TIMEOUT_JIFFIES;
	netdev->ethtool_ops = &lan78xx_ethtool_ops;

	dev->delta = 1;
	timer_setup(&dev->stat_monitor, lan78xx_stat_monitor, 0);

	mutex_init(&dev->stats.access_lock);

	if (intf->cur_altsetting->desc.bNumEndpoints < 3) {
		ret = -ENODEV;
		goto out4;
	}

	dev->pipe_in = usb_rcvbulkpipe(udev, BULK_IN_PIPE);
	ep_blkin = usb_pipe_endpoint(udev, dev->pipe_in);
	if (!ep_blkin || !usb_endpoint_is_bulk_in(&ep_blkin->desc)) {
		ret = -ENODEV;
		goto out4;
	}

	dev->pipe_out = usb_sndbulkpipe(udev, BULK_OUT_PIPE);
	ep_blkout = usb_pipe_endpoint(udev, dev->pipe_out);
	if (!ep_blkout || !usb_endpoint_is_bulk_out(&ep_blkout->desc)) {
		ret = -ENODEV;
		goto out4;
	}

	ep_intr = &intf->cur_altsetting->endpoint[2];
	if (!usb_endpoint_is_int_in(&ep_intr->desc)) {
		ret = -ENODEV;
		goto out4;
	}

	dev->pipe_intr = usb_rcvintpipe(dev->udev,
					usb_endpoint_num(&ep_intr->desc));

	ret = lan78xx_bind(dev, intf);
	if (ret < 0)
		goto out4;

	period = ep_intr->desc.bInterval;
	maxp = usb_maxpacket(dev->udev, dev->pipe_intr);

	dev->urb_intr = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->urb_intr) {
		ret = -ENOMEM;
		goto out5;
	}

	buf = kmalloc(maxp, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto free_urbs;
	}

	usb_fill_int_urb(dev->urb_intr, dev->udev,
			 dev->pipe_intr, buf, maxp,
			 intr_complete, dev, period);
	dev->urb_intr->transfer_flags |= URB_FREE_BUFFER;

	dev->maxpacket = usb_maxpacket(dev->udev, dev->pipe_out);

	/* Reject broken descriptors. */
	if (dev->maxpacket == 0) {
		ret = -ENODEV;
		goto free_urbs;
	}

	/* driver requires remote-wakeup capability during autosuspend. */
	intf->needs_remote_wakeup = 1;

	ret = lan78xx_phy_init(dev);
	if (ret < 0)
		goto free_urbs;

	ret = register_netdev(netdev);
	if (ret != 0) {
		netif_err(dev, probe, netdev, "couldn't register the device\n");
		goto phy_uninit;
	}

	usb_set_intfdata(intf, dev);

	ret = device_set_wakeup_enable(&udev->dev, true);

	 /* Default delay of 2sec has more overhead than advantage.
	  * Set to 10sec as default.
	  */
	pm_runtime_set_autosuspend_delay(&udev->dev,
					 DEFAULT_AUTOSUSPEND_DELAY);

	return 0;

phy_uninit:
	lan78xx_phy_uninit(dev);
free_urbs:
	usb_free_urb(dev->urb_intr);
out5:
	lan78xx_unbind(dev, intf);
out4:
	netif_napi_del(&dev->napi);
	lan78xx_free_rx_resources(dev);
out3:
	lan78xx_free_tx_resources(dev);
out2:
	free_netdev(netdev);
out1:
	usb_put_dev(udev);

	return ret;
}

static u16 lan78xx_wakeframe_crc16(const u8 *buf, int len)
{
	const u16 crc16poly = 0x8005;
	int i;
	u16 bit, crc, msb;
	u8 data;

	crc = 0xFFFF;
	for (i = 0; i < len; i++) {
		data = *buf++;
		for (bit = 0; bit < 8; bit++) {
			msb = crc >> 15;
			crc <<= 1;

			if (msb ^ (u16)(data & 1)) {
				crc ^= crc16poly;
				crc |= (u16)0x0001U;
			}
			data >>= 1;
		}
	}

	return crc;
}

static int lan78xx_set_auto_suspend(struct lan78xx_net *dev)
{
	u32 buf;
	int ret;

	ret = lan78xx_stop_tx_path(dev);
	if (ret < 0)
		return ret;

	ret = lan78xx_stop_rx_path(dev);
	if (ret < 0)
		return ret;

	/* auto suspend (selective suspend) */

	ret = lan78xx_write_reg(dev, WUCSR, 0);
	if (ret < 0)
		return ret;
	ret = lan78xx_write_reg(dev, WUCSR2, 0);
	if (ret < 0)
		return ret;
	ret = lan78xx_write_reg(dev, WK_SRC, 0xFFF1FF1FUL);
	if (ret < 0)
		return ret;

	/* set goodframe wakeup */

	ret = lan78xx_read_reg(dev, WUCSR, &buf);
	if (ret < 0)
		return ret;

	buf |= WUCSR_RFE_WAKE_EN_;
	buf |= WUCSR_STORE_WAKE_;

	ret = lan78xx_write_reg(dev, WUCSR, buf);
	if (ret < 0)
		return ret;

	ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
	if (ret < 0)
		return ret;

	buf &= ~PMT_CTL_RES_CLR_WKP_EN_;
	buf |= PMT_CTL_RES_CLR_WKP_STS_;
	buf |= PMT_CTL_PHY_WAKE_EN_;
	buf |= PMT_CTL_WOL_EN_;
	buf &= ~PMT_CTL_SUS_MODE_MASK_;
	buf |= PMT_CTL_SUS_MODE_3_;

	ret = lan78xx_write_reg(dev, PMT_CTL, buf);
	if (ret < 0)
		return ret;

	ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
	if (ret < 0)
		return ret;

	buf |= PMT_CTL_WUPS_MASK_;

	ret = lan78xx_write_reg(dev, PMT_CTL, buf);
	if (ret < 0)
		return ret;

	ret = lan78xx_start_rx_path(dev);

	return ret;
}

static int lan78xx_set_suspend(struct lan78xx_net *dev, u32 wol)
{
	const u8 ipv4_multicast[3] = { 0x01, 0x00, 0x5E };
	const u8 ipv6_multicast[3] = { 0x33, 0x33 };
	const u8 arp_type[2] = { 0x08, 0x06 };
	u32 temp_pmt_ctl;
	int mask_index;
	u32 temp_wucsr;
	u32 buf;
	u16 crc;
	int ret;

	ret = lan78xx_stop_tx_path(dev);
	if (ret < 0)
		return ret;
	ret = lan78xx_stop_rx_path(dev);
	if (ret < 0)
		return ret;

	ret = lan78xx_write_reg(dev, WUCSR, 0);
	if (ret < 0)
		return ret;
	ret = lan78xx_write_reg(dev, WUCSR2, 0);
	if (ret < 0)
		return ret;
	ret = lan78xx_write_reg(dev, WK_SRC, 0xFFF1FF1FUL);
	if (ret < 0)
		return ret;

	temp_wucsr = 0;

	temp_pmt_ctl = 0;

	ret = lan78xx_read_reg(dev, PMT_CTL, &temp_pmt_ctl);
	if (ret < 0)
		return ret;

	temp_pmt_ctl &= ~PMT_CTL_RES_CLR_WKP_EN_;
	temp_pmt_ctl |= PMT_CTL_RES_CLR_WKP_STS_;

	for (mask_index = 0; mask_index < NUM_OF_WUF_CFG; mask_index++) {
		ret = lan78xx_write_reg(dev, WUF_CFG(mask_index), 0);
		if (ret < 0)
			return ret;
	}

	mask_index = 0;
	if (wol & WAKE_PHY) {
		temp_pmt_ctl |= PMT_CTL_PHY_WAKE_EN_;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	if (wol & WAKE_MAGIC) {
		temp_wucsr |= WUCSR_MPEN_;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_3_;
	}
	if (wol & WAKE_BCAST) {
		temp_wucsr |= WUCSR_BCST_EN_;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	if (wol & WAKE_MCAST) {
		temp_wucsr |= WUCSR_WAKE_EN_;

		/* set WUF_CFG & WUF_MASK for IPv4 Multicast */
		crc = lan78xx_wakeframe_crc16(ipv4_multicast, 3);
		ret = lan78xx_write_reg(dev, WUF_CFG(mask_index),
					WUF_CFGX_EN_ |
					WUF_CFGX_TYPE_MCAST_ |
					(0 << WUF_CFGX_OFFSET_SHIFT_) |
					(crc & WUF_CFGX_CRC16_MASK_));
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, WUF_MASK0(mask_index), 7);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK1(mask_index), 0);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK2(mask_index), 0);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK3(mask_index), 0);
		if (ret < 0)
			return ret;

		mask_index++;

		/* for IPv6 Multicast */
		crc = lan78xx_wakeframe_crc16(ipv6_multicast, 2);
		ret = lan78xx_write_reg(dev, WUF_CFG(mask_index),
					WUF_CFGX_EN_ |
					WUF_CFGX_TYPE_MCAST_ |
					(0 << WUF_CFGX_OFFSET_SHIFT_) |
					(crc & WUF_CFGX_CRC16_MASK_));
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, WUF_MASK0(mask_index), 3);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK1(mask_index), 0);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK2(mask_index), 0);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK3(mask_index), 0);
		if (ret < 0)
			return ret;

		mask_index++;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	if (wol & WAKE_UCAST) {
		temp_wucsr |= WUCSR_PFDA_EN_;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	if (wol & WAKE_ARP) {
		temp_wucsr |= WUCSR_WAKE_EN_;

		/* set WUF_CFG & WUF_MASK
		 * for packettype (offset 12,13) = ARP (0x0806)
		 */
		crc = lan78xx_wakeframe_crc16(arp_type, 2);
		ret = lan78xx_write_reg(dev, WUF_CFG(mask_index),
					WUF_CFGX_EN_ |
					WUF_CFGX_TYPE_ALL_ |
					(0 << WUF_CFGX_OFFSET_SHIFT_) |
					(crc & WUF_CFGX_CRC16_MASK_));
		if (ret < 0)
			return ret;

		ret = lan78xx_write_reg(dev, WUF_MASK0(mask_index), 0x3000);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK1(mask_index), 0);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK2(mask_index), 0);
		if (ret < 0)
			return ret;
		ret = lan78xx_write_reg(dev, WUF_MASK3(mask_index), 0);
		if (ret < 0)
			return ret;

		mask_index++;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}

	ret = lan78xx_write_reg(dev, WUCSR, temp_wucsr);
	if (ret < 0)
		return ret;

	/* when multiple WOL bits are set */
	if (hweight_long((unsigned long)wol) > 1) {
		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	ret = lan78xx_write_reg(dev, PMT_CTL, temp_pmt_ctl);
	if (ret < 0)
		return ret;

	/* clear WUPS */
	ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
	if (ret < 0)
		return ret;

	buf |= PMT_CTL_WUPS_MASK_;

	ret = lan78xx_write_reg(dev, PMT_CTL, buf);
	if (ret < 0)
		return ret;

	ret = lan78xx_start_rx_path(dev);

	return ret;
}

static int lan78xx_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct lan78xx_net *dev = usb_get_intfdata(intf);
	bool dev_open;
	int ret;

	mutex_lock(&dev->dev_mutex);

	netif_dbg(dev, ifdown, dev->net,
		  "suspending: pm event %#x", message.event);

	dev_open = test_bit(EVENT_DEV_OPEN, &dev->flags);

	if (dev_open) {
		spin_lock_irq(&dev->txq.lock);
		/* don't autosuspend while transmitting */
		if ((skb_queue_len(&dev->txq) ||
		     skb_queue_len(&dev->txq_pend)) &&
		    PMSG_IS_AUTO(message)) {
			spin_unlock_irq(&dev->txq.lock);
			ret = -EBUSY;
			goto out;
		} else {
			set_bit(EVENT_DEV_ASLEEP, &dev->flags);
			spin_unlock_irq(&dev->txq.lock);
		}

		rtnl_lock();
		phylink_suspend(dev->phylink, false);
		rtnl_unlock();

		/* stop RX */
		ret = lan78xx_stop_rx_path(dev);
		if (ret < 0)
			goto out;

		ret = lan78xx_flush_rx_fifo(dev);
		if (ret < 0)
			goto out;

		/* stop Tx */
		ret = lan78xx_stop_tx_path(dev);
		if (ret < 0)
			goto out;

		/* empty out the Rx and Tx queues */
		netif_device_detach(dev->net);
		lan78xx_terminate_urbs(dev);
		usb_kill_urb(dev->urb_intr);

		/* reattach */
		netif_device_attach(dev->net);

		timer_delete(&dev->stat_monitor);

		if (PMSG_IS_AUTO(message)) {
			ret = lan78xx_set_auto_suspend(dev);
			if (ret < 0)
				goto out;
		} else {
			struct lan78xx_priv *pdata;

			pdata = (struct lan78xx_priv *)(dev->data[0]);
			netif_carrier_off(dev->net);
			ret = lan78xx_set_suspend(dev, pdata->wol);
			if (ret < 0)
				goto out;
		}
	} else {
		/* Interface is down; don't allow WOL and PHY
		 * events to wake up the host
		 */
		u32 buf;

		set_bit(EVENT_DEV_ASLEEP, &dev->flags);

		ret = lan78xx_write_reg(dev, WUCSR, 0);
		if (ret < 0)
			goto out;
		ret = lan78xx_write_reg(dev, WUCSR2, 0);
		if (ret < 0)
			goto out;

		ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
		if (ret < 0)
			goto out;

		buf &= ~PMT_CTL_RES_CLR_WKP_EN_;
		buf |= PMT_CTL_RES_CLR_WKP_STS_;
		buf &= ~PMT_CTL_SUS_MODE_MASK_;
		buf |= PMT_CTL_SUS_MODE_3_;

		ret = lan78xx_write_reg(dev, PMT_CTL, buf);
		if (ret < 0)
			goto out;

		ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
		if (ret < 0)
			goto out;

		buf |= PMT_CTL_WUPS_MASK_;

		ret = lan78xx_write_reg(dev, PMT_CTL, buf);
		if (ret < 0)
			goto out;
	}

	ret = 0;
out:
	mutex_unlock(&dev->dev_mutex);

	return ret;
}

static bool lan78xx_submit_deferred_urbs(struct lan78xx_net *dev)
{
	bool pipe_halted = false;
	struct urb *urb;

	while ((urb = usb_get_from_anchor(&dev->deferred))) {
		struct sk_buff *skb = urb->context;
		int ret;

		if (!netif_device_present(dev->net) ||
		    !netif_carrier_ok(dev->net) ||
		    pipe_halted) {
			lan78xx_release_tx_buf(dev, skb);
			continue;
		}

		ret = usb_submit_urb(urb, GFP_ATOMIC);

		if (ret == 0) {
			netif_trans_update(dev->net);
			lan78xx_queue_skb(&dev->txq, skb, tx_start);
		} else {
			if (ret == -EPIPE) {
				netif_stop_queue(dev->net);
				pipe_halted = true;
			} else if (ret == -ENODEV) {
				netif_device_detach(dev->net);
			}

			lan78xx_release_tx_buf(dev, skb);
		}
	}

	return pipe_halted;
}

static int lan78xx_resume(struct usb_interface *intf)
{
	struct lan78xx_net *dev = usb_get_intfdata(intf);
	bool dev_open;
	int ret;

	mutex_lock(&dev->dev_mutex);

	netif_dbg(dev, ifup, dev->net, "resuming device");

	dev_open = test_bit(EVENT_DEV_OPEN, &dev->flags);

	if (dev_open) {
		bool pipe_halted = false;

		ret = lan78xx_flush_tx_fifo(dev);
		if (ret < 0)
			goto out;

		if (dev->urb_intr) {
			int ret = usb_submit_urb(dev->urb_intr, GFP_KERNEL);

			if (ret < 0) {
				if (ret == -ENODEV)
					netif_device_detach(dev->net);
				netdev_warn(dev->net, "Failed to submit intr URB");
			}
		}

		spin_lock_irq(&dev->txq.lock);

		if (netif_device_present(dev->net)) {
			pipe_halted = lan78xx_submit_deferred_urbs(dev);

			if (pipe_halted)
				lan78xx_defer_kevent(dev, EVENT_TX_HALT);
		}

		clear_bit(EVENT_DEV_ASLEEP, &dev->flags);

		spin_unlock_irq(&dev->txq.lock);

		if (!pipe_halted &&
		    netif_device_present(dev->net) &&
		    (lan78xx_tx_pend_data_len(dev) < lan78xx_tx_urb_space(dev)))
			netif_start_queue(dev->net);

		ret = lan78xx_start_tx_path(dev);
		if (ret < 0)
			goto out;

		napi_schedule(&dev->napi);

		if (!timer_pending(&dev->stat_monitor)) {
			dev->delta = 1;
			mod_timer(&dev->stat_monitor,
				  jiffies + STAT_UPDATE_TIMER);
		}

	} else {
		clear_bit(EVENT_DEV_ASLEEP, &dev->flags);
	}

	ret = lan78xx_write_reg(dev, WUCSR2, 0);
	if (ret < 0)
		goto out;
	ret = lan78xx_write_reg(dev, WUCSR, 0);
	if (ret < 0)
		goto out;
	ret = lan78xx_write_reg(dev, WK_SRC, 0xFFF1FF1FUL);
	if (ret < 0)
		goto out;

	ret = lan78xx_write_reg(dev, WUCSR2, WUCSR2_NS_RCD_ |
					     WUCSR2_ARP_RCD_ |
					     WUCSR2_IPV6_TCPSYN_RCD_ |
					     WUCSR2_IPV4_TCPSYN_RCD_);
	if (ret < 0)
		goto out;

	ret = lan78xx_write_reg(dev, WUCSR, WUCSR_EEE_TX_WAKE_ |
					    WUCSR_EEE_RX_WAKE_ |
					    WUCSR_PFDA_FR_ |
					    WUCSR_RFE_WAKE_FR_ |
					    WUCSR_WUFR_ |
					    WUCSR_MPR_ |
					    WUCSR_BCST_FR_);
	if (ret < 0)
		goto out;

	ret = 0;
out:
	mutex_unlock(&dev->dev_mutex);

	return ret;
}

static int lan78xx_reset_resume(struct usb_interface *intf)
{
	struct lan78xx_net *dev = usb_get_intfdata(intf);
	int ret;

	netif_dbg(dev, ifup, dev->net, "(reset) resuming device");

	ret = lan78xx_reset(dev);
	if (ret < 0)
		return ret;

	ret = lan78xx_resume(intf);
	if (ret < 0)
		return ret;

	rtnl_lock();
	phylink_resume(dev->phylink);
	rtnl_unlock();

	return 0;
}

static const struct usb_device_id products[] = {
	{
	/* LAN7800 USB Gigabit Ethernet Device */
	USB_DEVICE(LAN78XX_USB_VENDOR_ID, LAN7800_USB_PRODUCT_ID),
	},
	{
	/* LAN7850 USB Gigabit Ethernet Device */
	USB_DEVICE(LAN78XX_USB_VENDOR_ID, LAN7850_USB_PRODUCT_ID),
	},
	{
	/* LAN7801 USB Gigabit Ethernet Device */
	USB_DEVICE(LAN78XX_USB_VENDOR_ID, LAN7801_USB_PRODUCT_ID),
	},
	{
	/* ATM2-AF USB Gigabit Ethernet Device */
	USB_DEVICE(AT29M2AF_USB_VENDOR_ID, AT29M2AF_USB_PRODUCT_ID),
	},
	{},
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver lan78xx_driver = {
	.name			= DRIVER_NAME,
	.id_table		= products,
	.probe			= lan78xx_probe,
	.disconnect		= lan78xx_disconnect,
	.suspend		= lan78xx_suspend,
	.resume			= lan78xx_resume,
	.reset_resume		= lan78xx_reset_resume,
	.supports_autosuspend	= 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(lan78xx_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
