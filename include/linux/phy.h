/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Framework and drivers for configuring and reading different PHYs
 * Based on code in sungem_phy.c and (long-removed) gianfar_phy.c
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 */

#ifndef __PHY_H
#define __PHY_H

#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/leds.h>
#include <linux/linkmode.h>
#include <linux/netlink.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/mii_timestamper.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/mod_devicetable.h>
#include <linux/u64_stats_sync.h>
#include <linux/irqreturn.h>
#include <linux/iopoll.h>
#include <linux/refcount.h>

#include <linux/atomic.h>
#include <net/eee.h>

extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_basic_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_basic_t1_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_basic_t1s_p2mp_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_gbit_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_gbit_fibre_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_10gbit_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_eee_cap1_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_eee_cap2_features) __ro_after_init;

#define PHY_BASIC_FEATURES ((unsigned long *)&phy_basic_features)
#define PHY_BASIC_T1_FEATURES ((unsigned long *)&phy_basic_t1_features)
#define PHY_BASIC_T1S_P2MP_FEATURES ((unsigned long *)&phy_basic_t1s_p2mp_features)
#define PHY_GBIT_FEATURES ((unsigned long *)&phy_gbit_features)
#define PHY_GBIT_FIBRE_FEATURES ((unsigned long *)&phy_gbit_fibre_features)
#define PHY_10GBIT_FEATURES ((unsigned long *)&phy_10gbit_features)
#define PHY_EEE_CAP1_FEATURES ((unsigned long *)&phy_eee_cap1_features)
#define PHY_EEE_CAP2_FEATURES ((unsigned long *)&phy_eee_cap2_features)

extern const int phy_basic_ports_array[3];

/*
 * Set phydev->irq to PHY_POLL if interrupts are not supported,
 * or not desired for this PHY.  Set to PHY_MAC_INTERRUPT if
 * the attached MAC driver handles the interrupt
 */
#define PHY_POLL		-1
#define PHY_MAC_INTERRUPT	-2

#define PHY_IS_INTERNAL		0x00000001
#define PHY_RST_AFTER_CLK_EN	0x00000002
#define PHY_POLL_CABLE_TEST	0x00000004
#define PHY_ALWAYS_CALL_SUSPEND	0x00000008
#define MDIO_DEVICE_IS_PHY	0x80000000

/**
 * enum phy_interface_t - Interface Mode definitions
 *
 * @PHY_INTERFACE_MODE_NA: Not Applicable - don't touch
 * @PHY_INTERFACE_MODE_INTERNAL: No interface, MAC and PHY combined
 * @PHY_INTERFACE_MODE_MII: Media-independent interface
 * @PHY_INTERFACE_MODE_GMII: Gigabit media-independent interface
 * @PHY_INTERFACE_MODE_SGMII: Serial gigabit media-independent interface
 * @PHY_INTERFACE_MODE_TBI: Ten Bit Interface
 * @PHY_INTERFACE_MODE_REVMII: Reverse Media Independent Interface
 * @PHY_INTERFACE_MODE_RMII: Reduced Media Independent Interface
 * @PHY_INTERFACE_MODE_REVRMII: Reduced Media Independent Interface in PHY role
 * @PHY_INTERFACE_MODE_RGMII: Reduced gigabit media-independent interface
 * @PHY_INTERFACE_MODE_RGMII_ID: RGMII with Internal RX+TX delay
 * @PHY_INTERFACE_MODE_RGMII_RXID: RGMII with Internal RX delay
 * @PHY_INTERFACE_MODE_RGMII_TXID: RGMII with Internal TX delay
 * @PHY_INTERFACE_MODE_RTBI: Reduced TBI
 * @PHY_INTERFACE_MODE_SMII: Serial MII
 * @PHY_INTERFACE_MODE_XGMII: 10 gigabit media-independent interface
 * @PHY_INTERFACE_MODE_XLGMII:40 gigabit media-independent interface
 * @PHY_INTERFACE_MODE_MOCA: Multimedia over Coax
 * @PHY_INTERFACE_MODE_PSGMII: Penta SGMII
 * @PHY_INTERFACE_MODE_QSGMII: Quad SGMII
 * @PHY_INTERFACE_MODE_TRGMII: Turbo RGMII
 * @PHY_INTERFACE_MODE_100BASEX: 100 BaseX
 * @PHY_INTERFACE_MODE_1000BASEX: 1000 BaseX
 * @PHY_INTERFACE_MODE_2500BASEX: 2500 BaseX
 * @PHY_INTERFACE_MODE_5GBASER: 5G BaseR
 * @PHY_INTERFACE_MODE_RXAUI: Reduced XAUI
 * @PHY_INTERFACE_MODE_XAUI: 10 Gigabit Attachment Unit Interface
 * @PHY_INTERFACE_MODE_10GBASER: 10G BaseR
 * @PHY_INTERFACE_MODE_25GBASER: 25G BaseR
 * @PHY_INTERFACE_MODE_USXGMII:  Universal Serial 10GE MII
 * @PHY_INTERFACE_MODE_10GKR: 10GBASE-KR - with Clause 73 AN
 * @PHY_INTERFACE_MODE_QUSGMII: Quad Universal SGMII
 * @PHY_INTERFACE_MODE_1000BASEKX: 1000Base-KX - with Clause 73 AN
 * @PHY_INTERFACE_MODE_10G_QXGMII: 10G-QXGMII - 4 ports over 10G USXGMII
 * @PHY_INTERFACE_MODE_50GBASER: 50GBase-R - with Clause 134 FEC
 * @PHY_INTERFACE_MODE_LAUI: 50 Gigabit Attachment Unit Interface
 * @PHY_INTERFACE_MODE_100GBASEP: 100GBase-P - with Clause 134 FEC
 * @PHY_INTERFACE_MODE_MIILITE: MII-Lite - MII without RXER TXER CRS COL
 * @PHY_INTERFACE_MODE_MAX: Book keeping
 *
 * Describes the interface between the MAC and PHY.
 */
typedef enum {
	PHY_INTERFACE_MODE_NA,
	PHY_INTERFACE_MODE_INTERNAL,
	PHY_INTERFACE_MODE_MII,
	PHY_INTERFACE_MODE_GMII,
	PHY_INTERFACE_MODE_SGMII,
	PHY_INTERFACE_MODE_TBI,
	PHY_INTERFACE_MODE_REVMII,
	PHY_INTERFACE_MODE_RMII,
	PHY_INTERFACE_MODE_REVRMII,
	PHY_INTERFACE_MODE_RGMII,
	PHY_INTERFACE_MODE_RGMII_ID,
	PHY_INTERFACE_MODE_RGMII_RXID,
	PHY_INTERFACE_MODE_RGMII_TXID,
	PHY_INTERFACE_MODE_RTBI,
	PHY_INTERFACE_MODE_SMII,
	PHY_INTERFACE_MODE_XGMII,
	PHY_INTERFACE_MODE_XLGMII,
	PHY_INTERFACE_MODE_MOCA,
	PHY_INTERFACE_MODE_PSGMII,
	PHY_INTERFACE_MODE_QSGMII,
	PHY_INTERFACE_MODE_TRGMII,
	PHY_INTERFACE_MODE_100BASEX,
	PHY_INTERFACE_MODE_1000BASEX,
	PHY_INTERFACE_MODE_2500BASEX,
	PHY_INTERFACE_MODE_5GBASER,
	PHY_INTERFACE_MODE_RXAUI,
	PHY_INTERFACE_MODE_XAUI,
	/* 10GBASE-R, XFI, SFI - single lane 10G Serdes */
	PHY_INTERFACE_MODE_10GBASER,
	PHY_INTERFACE_MODE_25GBASER,
	PHY_INTERFACE_MODE_USXGMII,
	/* 10GBASE-KR - with Clause 73 AN */
	PHY_INTERFACE_MODE_10GKR,
	PHY_INTERFACE_MODE_QUSGMII,
	PHY_INTERFACE_MODE_1000BASEKX,
	PHY_INTERFACE_MODE_10G_QXGMII,
	PHY_INTERFACE_MODE_50GBASER,
	PHY_INTERFACE_MODE_LAUI,
	PHY_INTERFACE_MODE_100GBASEP,
	PHY_INTERFACE_MODE_MIILITE,
	PHY_INTERFACE_MODE_MAX,
} phy_interface_t;

/* PHY interface mode bitmap handling */
#define DECLARE_PHY_INTERFACE_MASK(name) \
	DECLARE_BITMAP(name, PHY_INTERFACE_MODE_MAX)

static inline void phy_interface_zero(unsigned long *intf)
{
	bitmap_zero(intf, PHY_INTERFACE_MODE_MAX);
}

static inline bool phy_interface_empty(const unsigned long *intf)
{
	return bitmap_empty(intf, PHY_INTERFACE_MODE_MAX);
}

static inline unsigned int phy_interface_weight(const unsigned long *intf)
{
	return bitmap_weight(intf, PHY_INTERFACE_MODE_MAX);
}

static inline void phy_interface_and(unsigned long *dst, const unsigned long *a,
				     const unsigned long *b)
{
	bitmap_and(dst, a, b, PHY_INTERFACE_MODE_MAX);
}

static inline void phy_interface_or(unsigned long *dst, const unsigned long *a,
				    const unsigned long *b)
{
	bitmap_or(dst, a, b, PHY_INTERFACE_MODE_MAX);
}

static inline void phy_interface_set_rgmii(unsigned long *intf)
{
	__set_bit(PHY_INTERFACE_MODE_RGMII, intf);
	__set_bit(PHY_INTERFACE_MODE_RGMII_ID, intf);
	__set_bit(PHY_INTERFACE_MODE_RGMII_RXID, intf);
	__set_bit(PHY_INTERFACE_MODE_RGMII_TXID, intf);
}

/**
 * phy_modes - map phy_interface_t enum to device tree binding of phy-mode
 * @interface: enum phy_interface_t value
 *
 * Description: maps enum &phy_interface_t defined in this file
 * into the device tree binding of 'phy-mode', so that Ethernet
 * device driver can get PHY interface from device tree.
 */
static inline const char *phy_modes(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_NA:
		return "";
	case PHY_INTERFACE_MODE_INTERNAL:
		return "internal";
	case PHY_INTERFACE_MODE_MII:
		return "mii";
	case PHY_INTERFACE_MODE_GMII:
		return "gmii";
	case PHY_INTERFACE_MODE_SGMII:
		return "sgmii";
	case PHY_INTERFACE_MODE_TBI:
		return "tbi";
	case PHY_INTERFACE_MODE_REVMII:
		return "rev-mii";
	case PHY_INTERFACE_MODE_RMII:
		return "rmii";
	case PHY_INTERFACE_MODE_REVRMII:
		return "rev-rmii";
	case PHY_INTERFACE_MODE_RGMII:
		return "rgmii";
	case PHY_INTERFACE_MODE_RGMII_ID:
		return "rgmii-id";
	case PHY_INTERFACE_MODE_RGMII_RXID:
		return "rgmii-rxid";
	case PHY_INTERFACE_MODE_RGMII_TXID:
		return "rgmii-txid";
	case PHY_INTERFACE_MODE_RTBI:
		return "rtbi";
	case PHY_INTERFACE_MODE_SMII:
		return "smii";
	case PHY_INTERFACE_MODE_XGMII:
		return "xgmii";
	case PHY_INTERFACE_MODE_XLGMII:
		return "xlgmii";
	case PHY_INTERFACE_MODE_MOCA:
		return "moca";
	case PHY_INTERFACE_MODE_PSGMII:
		return "psgmii";
	case PHY_INTERFACE_MODE_QSGMII:
		return "qsgmii";
	case PHY_INTERFACE_MODE_TRGMII:
		return "trgmii";
	case PHY_INTERFACE_MODE_1000BASEX:
		return "1000base-x";
	case PHY_INTERFACE_MODE_1000BASEKX:
		return "1000base-kx";
	case PHY_INTERFACE_MODE_2500BASEX:
		return "2500base-x";
	case PHY_INTERFACE_MODE_5GBASER:
		return "5gbase-r";
	case PHY_INTERFACE_MODE_RXAUI:
		return "rxaui";
	case PHY_INTERFACE_MODE_XAUI:
		return "xaui";
	case PHY_INTERFACE_MODE_10GBASER:
		return "10gbase-r";
	case PHY_INTERFACE_MODE_25GBASER:
		return "25gbase-r";
	case PHY_INTERFACE_MODE_USXGMII:
		return "usxgmii";
	case PHY_INTERFACE_MODE_10GKR:
		return "10gbase-kr";
	case PHY_INTERFACE_MODE_100BASEX:
		return "100base-x";
	case PHY_INTERFACE_MODE_QUSGMII:
		return "qusgmii";
	case PHY_INTERFACE_MODE_10G_QXGMII:
		return "10g-qxgmii";
	case PHY_INTERFACE_MODE_50GBASER:
		return "50gbase-r";
	case PHY_INTERFACE_MODE_LAUI:
		return "laui";
	case PHY_INTERFACE_MODE_100GBASEP:
		return "100gbase-p";
	case PHY_INTERFACE_MODE_MIILITE:
		return "mii-lite";
	default:
		return "unknown";
	}
}

/**
 * rgmii_clock - map link speed to the clock rate
 * @speed: link speed value
 *
 * Description: maps RGMII supported link speeds into the clock rates.
 * This can also be used for MII, GMII, and RMII interface modes as the
 * clock rates are indentical, but the caller must be aware that errors
 * for unsupported clock rates will not be signalled.
 *
 * Returns: clock rate or negative errno
 */
static inline long rgmii_clock(int speed)
{
	switch (speed) {
	case SPEED_10:
		return 2500000;
	case SPEED_100:
		return 25000000;
	case SPEED_1000:
		return 125000000;
	default:
		return -EINVAL;
	}
}

#define PHY_MAX_ADDR	32

/* Used when trying to connect to a specific phy (mii bus id:phy device id) */
#define PHY_ID_FMT "%s:%02x"
#define PHY_ID_SIZE	(MII_BUS_ID_SIZE + 3)

#define MII_BUS_ID_SIZE	61

struct device;
struct kernel_hwtstamp_config;
struct phylink;
struct sfp_bus;
struct sfp_upstream_ops;
struct sk_buff;

/**
 * struct mdio_bus_stats - Statistics counters for MDIO busses
 * @transfers: Total number of transfers, i.e. @writes + @reads
 * @errors: Number of MDIO transfers that returned an error
 * @writes: Number of write transfers
 * @reads: Number of read transfers
 * @syncp: Synchronisation for incrementing statistics
 */
struct mdio_bus_stats {
	u64_stats_t transfers;
	u64_stats_t errors;
	u64_stats_t writes;
	u64_stats_t reads;
	/* Must be last, add new statistics above */
	struct u64_stats_sync syncp;
};

/**
 * struct mii_bus - Represents an MDIO bus
 *
 * @owner: Who owns this device
 * @name: User friendly name for this MDIO device, or driver name
 * @id: Unique identifier for this bus, typical from bus hierarchy
 * @priv: Driver private data
 *
 * The Bus class for PHYs.  Devices which provide access to
 * PHYs should register using this structure
 */
struct mii_bus {
	struct module *owner;
	const char *name;
	char id[MII_BUS_ID_SIZE];
	void *priv;
	/** @read: Perform a read transfer on the bus */
	int (*read)(struct mii_bus *bus, int addr, int regnum);
	/** @write: Perform a write transfer on the bus */
	int (*write)(struct mii_bus *bus, int addr, int regnum, u16 val);
	/** @read_c45: Perform a C45 read transfer on the bus */
	int (*read_c45)(struct mii_bus *bus, int addr, int devnum, int regnum);
	/** @write_c45: Perform a C45 write transfer on the bus */
	int (*write_c45)(struct mii_bus *bus, int addr, int devnum,
			 int regnum, u16 val);
	/** @reset: Perform a reset of the bus */
	int (*reset)(struct mii_bus *bus);

	/** @stats: Statistic counters per device on the bus */
	struct mdio_bus_stats stats[PHY_MAX_ADDR];

	/**
	 * @mdio_lock: A lock to ensure that only one thing can read/write
	 * the MDIO bus at a time
	 */
	struct mutex mdio_lock;

	/** @parent: Parent device of this bus */
	struct device *parent;
	/** @state: State of bus structure */
	enum {
		MDIOBUS_ALLOCATED = 1,
		MDIOBUS_REGISTERED,
		MDIOBUS_UNREGISTERED,
		MDIOBUS_RELEASED,
	} state;

	/** @dev: Kernel device representation */
	struct device dev;

	/** @mdio_map: list of all MDIO devices on bus */
	struct mdio_device *mdio_map[PHY_MAX_ADDR];

	/** @phy_mask: PHY addresses to be ignored when probing */
	u32 phy_mask;

	/** @phy_ignore_ta_mask: PHY addresses to ignore the TA/read failure */
	u32 phy_ignore_ta_mask;

	/**
	 * @irq: An array of interrupts, each PHY's interrupt at the index
	 * matching its address
	 */
	int irq[PHY_MAX_ADDR];

	/** @reset_delay_us: GPIO reset pulse width in microseconds */
	int reset_delay_us;
	/** @reset_post_delay_us: GPIO reset deassert delay in microseconds */
	int reset_post_delay_us;
	/** @reset_gpiod: Reset GPIO descriptor pointer */
	struct gpio_desc *reset_gpiod;

	/** @shared_lock: protect access to the shared element */
	struct mutex shared_lock;

#if IS_ENABLED(CONFIG_PHY_PACKAGE)
	/** @shared: shared state across different PHYs */
	struct phy_package_shared *shared[PHY_MAX_ADDR];
#endif
};
#define to_mii_bus(d) container_of(d, struct mii_bus, dev)

struct mii_bus *mdiobus_alloc_size(size_t size);

/**
 * mdiobus_alloc - Allocate an MDIO bus structure
 *
 * The internal state of the MDIO bus will be set of MDIOBUS_ALLOCATED ready
 * for the driver to register the bus.
 */
static inline struct mii_bus *mdiobus_alloc(void)
{
	return mdiobus_alloc_size(0);
}

int __mdiobus_register(struct mii_bus *bus, struct module *owner);
int __devm_mdiobus_register(struct device *dev, struct mii_bus *bus,
			    struct module *owner);
#define mdiobus_register(bus) __mdiobus_register(bus, THIS_MODULE)
#define devm_mdiobus_register(dev, bus) \
		__devm_mdiobus_register(dev, bus, THIS_MODULE)

void mdiobus_unregister(struct mii_bus *bus);
void mdiobus_free(struct mii_bus *bus);
struct mii_bus *devm_mdiobus_alloc_size(struct device *dev, int sizeof_priv);
static inline struct mii_bus *devm_mdiobus_alloc(struct device *dev)
{
	return devm_mdiobus_alloc_size(dev, 0);
}

struct mii_bus *mdio_find_bus(const char *mdio_name);
struct phy_device *mdiobus_scan_c22(struct mii_bus *bus, int addr);

#define PHY_INTERRUPT_DISABLED	false
#define PHY_INTERRUPT_ENABLED	true

/**
 * enum phy_state - PHY state machine states:
 *
 * @PHY_DOWN: PHY device and driver are not ready for anything.  probe
 * should be called if and only if the PHY is in this state,
 * given that the PHY device exists.
 * - PHY driver probe function will set the state to @PHY_READY
 *
 * @PHY_READY: PHY is ready to send and receive packets, but the
 * controller is not.  By default, PHYs which do not implement
 * probe will be set to this state by phy_probe().
 * - start will set the state to UP
 *
 * @PHY_UP: The PHY and attached device are ready to do work.
 * Interrupts should be started here.
 * - timer moves to @PHY_NOLINK or @PHY_RUNNING
 *
 * @PHY_NOLINK: PHY is up, but not currently plugged in.
 * - irq or timer will set @PHY_RUNNING if link comes back
 * - phy_stop moves to @PHY_HALTED
 *
 * @PHY_RUNNING: PHY is currently up, running, and possibly sending
 * and/or receiving packets
 * - irq or timer will set @PHY_NOLINK if link goes down
 * - phy_stop moves to @PHY_HALTED
 *
 * @PHY_CABLETEST: PHY is performing a cable test. Packet reception/sending
 * is not expected to work, carrier will be indicated as down. PHY will be
 * poll once per second, or on interrupt for it current state.
 * Once complete, move to UP to restart the PHY.
 * - phy_stop aborts the running test and moves to @PHY_HALTED
 *
 * @PHY_HALTED: PHY is up, but no polling or interrupts are done.
 * - phy_start moves to @PHY_UP
 *
 * @PHY_ERROR: PHY is up, but is in an error state.
 * - phy_stop moves to @PHY_HALTED
 */
enum phy_state {
	PHY_DOWN = 0,
	PHY_READY,
	PHY_HALTED,
	PHY_ERROR,
	PHY_UP,
	PHY_RUNNING,
	PHY_NOLINK,
	PHY_CABLETEST,
};

#define MDIO_MMD_NUM 32

/**
 * struct phy_c45_device_ids - 802.3-c45 Device Identifiers
 * @devices_in_package: IEEE 802.3 devices in package register value.
 * @mmds_present: bit vector of MMDs present.
 * @device_ids: The device identifer for each present device.
 */
struct phy_c45_device_ids {
	u32 devices_in_package;
	u32 mmds_present;
	u32 device_ids[MDIO_MMD_NUM];
};

struct macsec_context;
struct macsec_ops;

/**
 * struct phy_device - An instance of a PHY
 *
 * @mdio: MDIO bus this PHY is on
 * @drv: Pointer to the driver for this PHY instance
 * @devlink: Create a link between phy dev and mac dev, if the external phy
 *           used by current mac interface is managed by another mac interface.
 * @phyindex: Unique id across the phy's parent tree of phys to address the PHY
 *	      from userspace, similar to ifindex. A zero index means the PHY
 *	      wasn't assigned an id yet.
 * @phy_id: UID for this device found during discovery
 * @c45_ids: 802.3-c45 Device Identifiers if is_c45.
 * @is_c45:  Set to true if this PHY uses clause 45 addressing.
 * @is_internal: Set to true if this PHY is internal to a MAC.
 * @is_pseudo_fixed_link: Set to true if this PHY is an Ethernet switch, etc.
 * @is_gigabit_capable: Set to true if PHY supports 1000Mbps
 * @has_fixups: Set to true if this PHY has fixups/quirks.
 * @suspended: Set to true if this PHY has been suspended successfully.
 * @suspended_by_mdio_bus: Set to true if this PHY was suspended by MDIO bus.
 * @sysfs_links: Internal boolean tracking sysfs symbolic links setup/removal.
 * @loopback_enabled: Set true if this PHY has been loopbacked successfully.
 * @downshifted_rate: Set true if link speed has been downshifted.
 * @is_on_sfp_module: Set true if PHY is located on an SFP module.
 * @mac_managed_pm: Set true if MAC driver takes of suspending/resuming PHY
 * @wol_enabled: Set to true if the PHY or the attached MAC have Wake-on-LAN
 * 		 enabled.
 * @is_genphy_driven: PHY is driven by one of the generic PHY drivers
 * @state: State of the PHY for management purposes
 * @dev_flags: Device-specific flags used by the PHY driver.
 *
 *      - Bits [15:0] are free to use by the PHY driver to communicate
 *        driver specific behavior.
 *      - Bits [23:16] are currently reserved for future use.
 *      - Bits [31:24] are reserved for defining generic
 *        PHY driver behavior.
 * @irq: IRQ number of the PHY's interrupt (-1 if none)
 * @phylink: Pointer to phylink instance for this PHY
 * @sfp_bus_attached: Flag indicating whether the SFP bus has been attached
 * @sfp_bus: SFP bus attached to this PHY's fiber port
 * @attached_dev: The attached enet driver's device instance ptr
 * @adjust_link: Callback for the enet controller to respond to changes: in the
 *               link state.
 * @phy_link_change: Callback for phylink for notification of link change
 * @macsec_ops: MACsec offloading ops.
 *
 * @speed: Current link speed
 * @duplex: Current duplex
 * @port: Current port
 * @pause: Current pause
 * @asym_pause: Current asymmetric pause
 * @supported: Combined MAC/PHY supported linkmodes
 * @advertising: Currently advertised linkmodes
 * @adv_old: Saved advertised while power saving for WoL
 * @supported_eee: supported PHY EEE linkmodes
 * @advertising_eee: Currently advertised EEE linkmodes
 * @enable_tx_lpi: When True, MAC should transmit LPI to PHY
 * @eee_active: phylib private state, indicating that EEE has been negotiated
 * @eee_cfg: User configuration of EEE
 * @lp_advertising: Current link partner advertised linkmodes
 * @host_interfaces: PHY interface modes supported by host
 * @eee_disabled_modes: Energy efficient ethernet modes not to be advertised
 * @autoneg: Flag autoneg being used
 * @rate_matching: Current rate matching mode
 * @link: Current link state
 * @autoneg_complete: Flag auto negotiation of the link has completed
 * @mdix: Current crossover
 * @mdix_ctrl: User setting of crossover
 * @pma_extable: Cached value of PMA/PMD Extended Abilities Register
 * @interrupts: Flag interrupts have been enabled
 * @irq_suspended: Flag indicating PHY is suspended and therefore interrupt
 *                 handling shall be postponed until PHY has resumed
 * @irq_rerun: Flag indicating interrupts occurred while PHY was suspended,
 *             requiring a rerun of the interrupt handler after resume
 * @default_timestamp: Flag indicating whether we are using the phy
 *		       timestamp as the default one
 * @interface: enum phy_interface_t value
 * @possible_interfaces: bitmap if interface modes that the attached PHY
 *			 will switch between depending on media speed.
 * @skb: Netlink message for cable diagnostics
 * @nest: Netlink nest used for cable diagnostics
 * @ehdr: nNtlink header for cable diagnostics
 * @phy_led_triggers: Array of LED triggers
 * @phy_num_led_triggers: Number of triggers in @phy_led_triggers
 * @led_link_trigger: LED trigger for link up/down
 * @last_triggered: last LED trigger for link speed
 * @leds: list of PHY LED structures
 * @master_slave_set: User requested master/slave configuration
 * @master_slave_get: Current master/slave advertisement
 * @master_slave_state: Current master/slave configuration
 * @mii_ts: Pointer to time stamper callbacks
 * @psec: Pointer to Power Sourcing Equipment control struct
 * @lock:  Mutex for serialization access to PHY
 * @state_queue: Work queue for state machine
 * @link_down_events: Number of times link was lost
 * @shared: Pointer to private data shared by phys in one package
 * @priv: Pointer to driver private data
 *
 * interrupts currently only supports enabled or disabled,
 * but could be changed in the future to support enabling
 * and disabling specific interrupts
 *
 * Contains some infrastructure for polling and interrupt
 * handling, as well as handling shifts in PHY hardware state
 */
struct phy_device {
	struct mdio_device mdio;

	/* Information about the PHY type */
	/* And management functions */
	const struct phy_driver *drv;

	struct device_link *devlink;

	u32 phyindex;
	u32 phy_id;

	struct phy_c45_device_ids c45_ids;
	unsigned is_c45:1;
	unsigned is_internal:1;
	unsigned is_pseudo_fixed_link:1;
	unsigned is_gigabit_capable:1;
	unsigned has_fixups:1;
	unsigned suspended:1;
	unsigned suspended_by_mdio_bus:1;
	unsigned sysfs_links:1;
	unsigned loopback_enabled:1;
	unsigned downshifted_rate:1;
	unsigned is_on_sfp_module:1;
	unsigned mac_managed_pm:1;
	unsigned wol_enabled:1;
	unsigned is_genphy_driven:1;

	unsigned autoneg:1;
	/* The most recently read link state */
	unsigned link:1;
	unsigned autoneg_complete:1;

	/* Interrupts are enabled */
	unsigned interrupts:1;
	unsigned irq_suspended:1;
	unsigned irq_rerun:1;

	unsigned default_timestamp:1;

	int rate_matching;

	enum phy_state state;

	u32 dev_flags;

	phy_interface_t interface;
	DECLARE_PHY_INTERFACE_MASK(possible_interfaces);

	/*
	 * forced speed & duplex (no autoneg)
	 * partner speed & duplex & pause (autoneg)
	 */
	int speed;
	int duplex;
	int port;
	int pause;
	int asym_pause;
	u8 master_slave_get;
	u8 master_slave_set;
	u8 master_slave_state;

	/* Union of PHY and Attached devices' supported link modes */
	/* See ethtool.h for more info */
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(lp_advertising);
	/* used with phy_speed_down */
	__ETHTOOL_DECLARE_LINK_MODE_MASK(adv_old);
	/* used for eee validation and configuration*/
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported_eee);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising_eee);
	/* Energy efficient ethernet modes which should be prohibited */
	__ETHTOOL_DECLARE_LINK_MODE_MASK(eee_disabled_modes);
	bool enable_tx_lpi;
	bool eee_active;
	struct eee_config eee_cfg;

	/* Host supported PHY interface types. Should be ignored if empty. */
	DECLARE_PHY_INTERFACE_MASK(host_interfaces);

#ifdef CONFIG_LED_TRIGGER_PHY
	struct phy_led_trigger *phy_led_triggers;
	unsigned int phy_num_led_triggers;
	struct phy_led_trigger *last_triggered;

	struct phy_led_trigger *led_link_trigger;
#endif
	struct list_head leds;

	/*
	 * Interrupt number for this PHY
	 * -1 means no interrupt
	 */
	int irq;

	/* private data pointer */
	/* For use by PHYs to maintain extra state */
	void *priv;

#if IS_ENABLED(CONFIG_PHY_PACKAGE)
	/* shared data pointer */
	/* For use by PHYs inside the same package that need a shared state. */
	struct phy_package_shared *shared;
#endif

	/* Reporting cable test results */
	struct sk_buff *skb;
	void *ehdr;
	struct nlattr *nest;

	/* Interrupt and Polling infrastructure */
	struct delayed_work state_queue;

	struct mutex lock;

	/* This may be modified under the rtnl lock */
	bool sfp_bus_attached;
	struct sfp_bus *sfp_bus;
	struct phylink *phylink;
	struct net_device *attached_dev;
	struct mii_timestamper *mii_ts;
	struct pse_control *psec;

	u8 mdix;
	u8 mdix_ctrl;

	int pma_extable;

	unsigned int link_down_events;

	void (*phy_link_change)(struct phy_device *phydev, bool up);
	void (*adjust_link)(struct net_device *dev);

#if IS_ENABLED(CONFIG_MACSEC)
	/* MACsec management functions */
	const struct macsec_ops *macsec_ops;
#endif
};

/* Generic phy_device::dev_flags */
#define PHY_F_NO_IRQ		0x80000000
#define PHY_F_RXC_ALWAYS_ON	0x40000000

#define to_phy_device(__dev)	container_of_const(to_mdio_device(__dev), struct phy_device, mdio)

/**
 * struct phy_tdr_config - Configuration of a TDR raw test
 *
 * @first: Distance for first data collection point
 * @last: Distance for last data collection point
 * @step: Step between data collection points
 * @pair: Bitmap of cable pairs to collect data for
 *
 * A structure containing possible configuration parameters
 * for a TDR cable test. The driver does not need to implement
 * all the parameters, but should report what is actually used.
 * All distances are in centimeters.
 */
struct phy_tdr_config {
	u32 first;
	u32 last;
	u32 step;
	s8 pair;
};
#define PHY_PAIR_ALL -1

/**
 * enum link_inband_signalling - in-band signalling modes that are supported
 *
 * @LINK_INBAND_DISABLE: in-band signalling can be disabled
 * @LINK_INBAND_ENABLE: in-band signalling can be enabled without bypass
 * @LINK_INBAND_BYPASS: in-band signalling can be enabled with bypass
 *
 * The possible and required bits can only be used if the valid bit is set.
 * If possible is clear, that means inband signalling can not be used.
 * Required is only valid when possible is set, and means that inband
 * signalling must be used.
 */
enum link_inband_signalling {
	LINK_INBAND_DISABLE		= BIT(0),
	LINK_INBAND_ENABLE		= BIT(1),
	LINK_INBAND_BYPASS		= BIT(2),
};

/**
 * struct phy_plca_cfg - Configuration of the PLCA (Physical Layer Collision
 * Avoidance) Reconciliation Sublayer.
 *
 * @version: read-only PLCA register map version. -1 = not available. Ignored
 *   when setting the configuration. Format is the same as reported by the PLCA
 *   IDVER register (31.CA00). -1 = not available.
 * @enabled: PLCA configured mode (enabled/disabled). -1 = not available / don't
 *   set. 0 = disabled, anything else = enabled.
 * @node_id: the PLCA local node identifier. -1 = not available / don't set.
 *   Allowed values [0 .. 254]. 255 = node disabled.
 * @node_cnt: the PLCA node count (maximum number of nodes having a TO). Only
 *   meaningful for the coordinator (node_id = 0). -1 = not available / don't
 *   set. Allowed values [1 .. 255].
 * @to_tmr: The value of the PLCA to_timer in bit-times, which determines the
 *   PLCA transmit opportunity window opening. See IEEE802.3 Clause 148 for
 *   more details. The to_timer shall be set equal over all nodes.
 *   -1 = not available / don't set. Allowed values [0 .. 255].
 * @burst_cnt: controls how many additional frames a node is allowed to send in
 *   single transmit opportunity (TO). The default value of 0 means that the
 *   node is allowed exactly one frame per TO. A value of 1 allows two frames
 *   per TO, and so on. -1 = not available / don't set.
 *   Allowed values [0 .. 255].
 * @burst_tmr: controls how many bit times to wait for the MAC to send a new
 *   frame before interrupting the burst. This value should be set to a value
 *   greater than the MAC inter-packet gap (which is typically 96 bits).
 *   -1 = not available / don't set. Allowed values [0 .. 255].
 *
 * A structure containing configuration parameters for setting/getting the PLCA
 * RS configuration. The driver does not need to implement all the parameters,
 * but should report what is actually used.
 */
struct phy_plca_cfg {
	int version;
	int enabled;
	int node_id;
	int node_cnt;
	int to_tmr;
	int burst_cnt;
	int burst_tmr;
};

/**
 * struct phy_plca_status - Status of the PLCA (Physical Layer Collision
 * Avoidance) Reconciliation Sublayer.
 *
 * @pst: The PLCA status as reported by the PST bit in the PLCA STATUS
 *	register(31.CA03), indicating BEACON activity.
 *
 * A structure containing status information of the PLCA RS configuration.
 * The driver does not need to implement all the parameters, but should report
 * what is actually used.
 */
struct phy_plca_status {
	bool pst;
};

/* Modes for PHY LED configuration */
enum phy_led_modes {
	PHY_LED_ACTIVE_HIGH = 0,
	PHY_LED_ACTIVE_LOW = 1,
	PHY_LED_INACTIVE_HIGH_IMPEDANCE = 2,

	/* keep it last */
	__PHY_LED_MODES_NUM,
};

/**
 * struct phy_led: An LED driven by the PHY
 *
 * @list: List of LEDs
 * @phydev: PHY this LED is attached to
 * @led_cdev: Standard LED class structure
 * @index: Number of the LED
 */
struct phy_led {
	struct list_head list;
	struct phy_device *phydev;
	struct led_classdev led_cdev;
	u8 index;
};

#define to_phy_led(d) container_of(d, struct phy_led, led_cdev)

/**
 * struct phy_driver - Driver structure for a particular PHY type
 *
 * @mdiodrv: Data common to all MDIO devices
 * @phy_id: The result of reading the UID registers of this PHY
 *   type, and ANDing them with the phy_id_mask.  This driver
 *   only works for PHYs with IDs which match this field
 * @name: The friendly name of this PHY type
 * @phy_id_mask: Defines the important bits of the phy_id
 * @features: A mandatory list of features (speed, duplex, etc)
 *   supported by this PHY
 * @flags: A bitfield defining certain other features this PHY
 *   supports (like interrupts)
 * @driver_data: Static driver data
 *
 * All functions are optional. If config_aneg or read_status
 * are not implemented, the phy core uses the genphy versions.
 * Note that none of these functions should be called from
 * interrupt time. The goal is for the bus read/write functions
 * to be able to block when the bus transaction is happening,
 * and be freed up by an interrupt (The MPC85xx has this ability,
 * though it is not currently supported in the driver).
 */
struct phy_driver {
	struct mdio_driver_common mdiodrv;
	u32 phy_id;
	char *name;
	u32 phy_id_mask;
	const unsigned long * const features;
	u32 flags;
	const void *driver_data;

	/**
	 * @soft_reset: Called to issue a PHY software reset
	 */
	int (*soft_reset)(struct phy_device *phydev);

	/**
	 * @config_init: Called to initialize the PHY,
	 * including after a reset
	 */
	int (*config_init)(struct phy_device *phydev);

	/**
	 * @probe: Called during discovery.  Used to set
	 * up device-specific structures, if any
	 */
	int (*probe)(struct phy_device *phydev);

	/**
	 * @get_features: Probe the hardware to determine what
	 * abilities it has.  Should only set phydev->supported.
	 */
	int (*get_features)(struct phy_device *phydev);

	/**
	 * @inband_caps: query whether in-band is supported for the given PHY
	 * interface mode. Returns a bitmask of bits defined by enum
	 * link_inband_signalling.
	 */
	unsigned int (*inband_caps)(struct phy_device *phydev,
				    phy_interface_t interface);

	/**
	 * @config_inband: configure in-band mode for the PHY
	 */
	int (*config_inband)(struct phy_device *phydev, unsigned int modes);

	/**
	 * @get_rate_matching: Get the supported type of rate matching for a
	 * particular phy interface. This is used by phy consumers to determine
	 * whether to advertise lower-speed modes for that interface. It is
	 * assumed that if a rate matching mode is supported on an interface,
	 * then that interface's rate can be adapted to all slower link speeds
	 * supported by the phy. If the interface is not supported, this should
	 * return %RATE_MATCH_NONE.
	 */
	int (*get_rate_matching)(struct phy_device *phydev,
				   phy_interface_t iface);

	/* PHY Power Management */
	/** @suspend: Suspend the hardware, saving state if needed */
	int (*suspend)(struct phy_device *phydev);
	/** @resume: Resume the hardware, restoring state if needed */
	int (*resume)(struct phy_device *phydev);

	/**
	 * @config_aneg: Configures the advertisement and resets
	 * autonegotiation if phydev->autoneg is on,
	 * forces the speed to the current settings in phydev
	 * if phydev->autoneg is off
	 */
	int (*config_aneg)(struct phy_device *phydev);

	/** @aneg_done: Determines the auto negotiation result */
	int (*aneg_done)(struct phy_device *phydev);

	/** @read_status: Determines the negotiated speed and duplex */
	int (*read_status)(struct phy_device *phydev);

	/**
	 * @config_intr: Enables or disables interrupts.
	 * It should also clear any pending interrupts prior to enabling the
	 * IRQs and after disabling them.
	 */
	int (*config_intr)(struct phy_device *phydev);

	/** @handle_interrupt: Override default interrupt handling */
	irqreturn_t (*handle_interrupt)(struct phy_device *phydev);

	/** @remove: Clears up any memory if needed */
	void (*remove)(struct phy_device *phydev);

	/**
	 * @match_phy_device: Returns true if this is a suitable
	 * driver for the given phydev.	 If NULL, matching is based on
	 * phy_id and phy_id_mask.
	 */
	int (*match_phy_device)(struct phy_device *phydev,
				const struct phy_driver *phydrv);

	/**
	 * @set_wol: Some devices (e.g. qnap TS-119P II) require PHY
	 * register changes to enable Wake on LAN, so set_wol is
	 * provided to be called in the ethernet driver's set_wol
	 * function.
	 */
	int (*set_wol)(struct phy_device *dev, struct ethtool_wolinfo *wol);

	/**
	 * @get_wol: See set_wol, but for checking whether Wake on LAN
	 * is enabled.
	 */
	void (*get_wol)(struct phy_device *dev, struct ethtool_wolinfo *wol);

	/**
	 * @link_change_notify: Called to inform a PHY device driver
	 * when the core is about to change the link state. This
	 * callback is supposed to be used as fixup hook for drivers
	 * that need to take action when the link state
	 * changes. Drivers are by no means allowed to mess with the
	 * PHY device structure in their implementations.
	 */
	void (*link_change_notify)(struct phy_device *dev);

	/**
	 * @read_mmd: PHY specific driver override for reading a MMD
	 * register.  This function is optional for PHY specific
	 * drivers.  When not provided, the default MMD read function
	 * will be used by phy_read_mmd(), which will use either a
	 * direct read for Clause 45 PHYs or an indirect read for
	 * Clause 22 PHYs.  devnum is the MMD device number within the
	 * PHY device, regnum is the register within the selected MMD
	 * device.
	 */
	int (*read_mmd)(struct phy_device *dev, int devnum, u16 regnum);

	/**
	 * @write_mmd: PHY specific driver override for writing a MMD
	 * register.  This function is optional for PHY specific
	 * drivers.  When not provided, the default MMD write function
	 * will be used by phy_write_mmd(), which will use either a
	 * direct write for Clause 45 PHYs, or an indirect write for
	 * Clause 22 PHYs.  devnum is the MMD device number within the
	 * PHY device, regnum is the register within the selected MMD
	 * device.  val is the value to be written.
	 */
	int (*write_mmd)(struct phy_device *dev, int devnum, u16 regnum,
			 u16 val);

	/** @read_page: Return the current PHY register page number */
	int (*read_page)(struct phy_device *dev);
	/** @write_page: Set the current PHY register page number */
	int (*write_page)(struct phy_device *dev, int page);

	/**
	 * @module_info: Get the size and type of the eeprom contained
	 * within a plug-in module
	 */
	int (*module_info)(struct phy_device *dev,
			   struct ethtool_modinfo *modinfo);

	/**
	 * @module_eeprom: Get the eeprom information from the plug-in
	 * module
	 */
	int (*module_eeprom)(struct phy_device *dev,
			     struct ethtool_eeprom *ee, u8 *data);

	/** @cable_test_start: Start a cable test */
	int (*cable_test_start)(struct phy_device *dev);

	/**  @cable_test_tdr_start: Start a raw TDR cable test */
	int (*cable_test_tdr_start)(struct phy_device *dev,
				    const struct phy_tdr_config *config);

	/**
	 * @cable_test_get_status: Once per second, or on interrupt,
	 * request the status of the test.
	 */
	int (*cable_test_get_status)(struct phy_device *dev, bool *finished);

	/* Get statistics from the PHY using ethtool */
	/**
	 * @get_phy_stats: Retrieve PHY statistics.
	 * @dev: The PHY device for which the statistics are retrieved.
	 * @eth_stats: structure where Ethernet PHY stats will be stored.
	 * @stats: structure where additional PHY-specific stats will be stored.
	 *
	 * Retrieves the supported PHY statistics and populates the provided
	 * structures. The input structures are pre-initialized with
	 * `ETHTOOL_STAT_NOT_SET`, and the driver must only modify members
	 * corresponding to supported statistics. Unmodified members will remain
	 * set to `ETHTOOL_STAT_NOT_SET` and will not be returned to userspace.
	 */
	void (*get_phy_stats)(struct phy_device *dev,
			      struct ethtool_eth_phy_stats *eth_stats,
			      struct ethtool_phy_stats *stats);

	/**
	 * @get_link_stats: Retrieve link statistics.
	 * @dev: The PHY device for which the statistics are retrieved.
	 * @link_stats: structure where link-specific stats will be stored.
	 *
	 * Retrieves link-related statistics for the given PHY device. The input
	 * structure is pre-initialized with `ETHTOOL_STAT_NOT_SET`, and the
	 * driver must only modify members corresponding to supported
	 * statistics. Unmodified members will remain set to
	 * `ETHTOOL_STAT_NOT_SET` and will not be returned to userspace.
	 */
	void (*get_link_stats)(struct phy_device *dev,
			       struct ethtool_link_ext_stats *link_stats);

	/**
	 * @update_stats: Trigger periodic statistics updates.
	 * @dev: The PHY device for which statistics updates are triggered.
	 *
	 * Periodically gathers statistics from the PHY device to update locally
	 * maintained 64-bit counters. This is necessary for PHYs that implement
	 * reduced-width counters (e.g., 16-bit or 32-bit) which can overflow
	 * more frequently compared to 64-bit counters. By invoking this
	 * callback, drivers can fetch the current counter values, handle
	 * overflow detection, and accumulate the results into local 64-bit
	 * counters for accurate reporting through the `get_phy_stats` and
	 * `get_link_stats` interfaces.
	 *
	 * Return: 0 on success or a negative error code on failure.
	 */
	int (*update_stats)(struct phy_device *dev);

	/** @get_sset_count: Number of statistic counters */
	int (*get_sset_count)(struct phy_device *dev);
	/** @get_strings: Names of the statistic counters */
	void (*get_strings)(struct phy_device *dev, u8 *data);
	/** @get_stats: Return the statistic counter values */
	void (*get_stats)(struct phy_device *dev,
			  struct ethtool_stats *stats, u64 *data);

	/* Get and Set PHY tunables */
	/** @get_tunable: Return the value of a tunable */
	int (*get_tunable)(struct phy_device *dev,
			   struct ethtool_tunable *tuna, void *data);
	/** @set_tunable: Set the value of a tunable */
	int (*set_tunable)(struct phy_device *dev,
			    struct ethtool_tunable *tuna,
			    const void *data);
	/**
	 * @set_loopback: Set the loopback mode of the PHY
	 * enable selects if the loopback mode is enabled or disabled. If the
	 * loopback mode is enabled, then the speed of the loopback mode can be
	 * requested with the speed argument. If the speed argument is zero,
	 * then any speed can be selected. If the speed argument is > 0, then
	 * this speed shall be selected for the loopback mode or EOPNOTSUPP
	 * shall be returned if speed selection is not supported.
	 */
	int (*set_loopback)(struct phy_device *dev, bool enable, int speed);
	/** @get_sqi: Get the signal quality indication */
	int (*get_sqi)(struct phy_device *dev);
	/** @get_sqi_max: Get the maximum signal quality indication */
	int (*get_sqi_max)(struct phy_device *dev);

	/* PLCA RS interface */
	/** @get_plca_cfg: Return the current PLCA configuration */
	int (*get_plca_cfg)(struct phy_device *dev,
			    struct phy_plca_cfg *plca_cfg);
	/** @set_plca_cfg: Set the PLCA configuration */
	int (*set_plca_cfg)(struct phy_device *dev,
			    const struct phy_plca_cfg *plca_cfg);
	/** @get_plca_status: Return the current PLCA status info */
	int (*get_plca_status)(struct phy_device *dev,
			       struct phy_plca_status *plca_st);

	/**
	 * @led_brightness_set: Set a PHY LED brightness. Index
	 * indicates which of the PHYs led should be set. Value
	 * follows the standard LED class meaning, e.g. LED_OFF,
	 * LED_HALF, LED_FULL.
	 */
	int (*led_brightness_set)(struct phy_device *dev,
				  u8 index, enum led_brightness value);

	/**
	 * @led_blink_set: Set a PHY LED blinking.  Index indicates
	 * which of the PHYs led should be configured to blink. Delays
	 * are in milliseconds and if both are zero then a sensible
	 * default should be chosen.  The call should adjust the
	 * timings in that case and if it can't match the values
	 * specified exactly.
	 */
	int (*led_blink_set)(struct phy_device *dev, u8 index,
			     unsigned long *delay_on,
			     unsigned long *delay_off);
	/**
	 * @led_hw_is_supported: Can the HW support the given rules.
	 * @dev: PHY device which has the LED
	 * @index: Which LED of the PHY device
	 * @rules The core is interested in these rules
	 *
	 * Return 0 if yes,  -EOPNOTSUPP if not, or an error code.
	 */
	int (*led_hw_is_supported)(struct phy_device *dev, u8 index,
				   unsigned long rules);
	/**
	 * @led_hw_control_set: Set the HW to control the LED
	 * @dev: PHY device which has the LED
	 * @index: Which LED of the PHY device
	 * @rules The rules used to control the LED
	 *
	 * Returns 0, or a an error code.
	 */
	int (*led_hw_control_set)(struct phy_device *dev, u8 index,
				  unsigned long rules);
	/**
	 * @led_hw_control_get: Get how the HW is controlling the LED
	 * @dev: PHY device which has the LED
	 * @index: Which LED of the PHY device
	 * @rules Pointer to the rules used to control the LED
	 *
	 * Set *@rules to how the HW is currently blinking. Returns 0
	 * on success, or a error code if the current blinking cannot
	 * be represented in rules, or some other error happens.
	 */
	int (*led_hw_control_get)(struct phy_device *dev, u8 index,
				  unsigned long *rules);

	/**
	 * @led_polarity_set: Set the LED polarity modes
	 * @dev: PHY device which has the LED
	 * @index: Which LED of the PHY device
	 * @modes: bitmap of LED polarity modes
	 *
	 * Configure LED with all the required polarity modes in @modes
	 * to make it correctly turn ON or OFF.
	 *
	 * Returns 0, or an error code.
	 */
	int (*led_polarity_set)(struct phy_device *dev, int index,
				unsigned long modes);

	/**
	 * @get_next_update_time: Get the time until the next update event
	 * @dev: PHY device
	 *
	 * Callback to determine the time (in jiffies) until the next
	 * update event for the PHY state  machine. Allows PHY drivers to
	 * dynamically adjust polling intervals based on link state or other
	 * conditions.
	 *
	 * Returns the time in jiffies until the next update event.
	 */
	unsigned int (*get_next_update_time)(struct phy_device *dev);
};
#define to_phy_driver(d) container_of_const(to_mdio_common_driver(d),		\
				      struct phy_driver, mdiodrv)

#define PHY_ID_MATCH_EXTACT_MASK GENMASK(31, 0)
#define PHY_ID_MATCH_MODEL_MASK GENMASK(31, 4)
#define PHY_ID_MATCH_VENDOR_MASK GENMASK(31, 10)

#define PHY_ID_MATCH_EXACT(id) .phy_id = (id), .phy_id_mask = PHY_ID_MATCH_EXTACT_MASK
#define PHY_ID_MATCH_MODEL(id) .phy_id = (id), .phy_id_mask = PHY_ID_MATCH_MODEL_MASK
#define PHY_ID_MATCH_VENDOR(id) .phy_id = (id), .phy_id_mask = PHY_ID_MATCH_VENDOR_MASK

/**
 * phy_id_compare - compare @id1 with @id2 taking account of @mask
 * @id1: first PHY ID
 * @id2: second PHY ID
 * @mask: the PHY ID mask, set bits are significant in matching
 *
 * Return true if the bits from @id1 and @id2 specified by @mask match.
 * This uses an equivalent test to (@id & @mask) == (@phy_id & @mask).
 */
static inline bool phy_id_compare(u32 id1, u32 id2, u32 mask)
{
	return !((id1 ^ id2) & mask);
}

/**
 * phy_id_compare_vendor - compare @id with @vendor mask
 * @id: PHY ID
 * @vendor_mask: PHY Vendor mask
 *
 * Return: true if the bits from @id match @vendor using the
 *	   generic PHY Vendor mask.
 */
static inline bool phy_id_compare_vendor(u32 id, u32 vendor_mask)
{
	return phy_id_compare(id, vendor_mask, PHY_ID_MATCH_VENDOR_MASK);
}

/**
 * phydev_id_compare - compare @id with the PHY's Clause 22 ID
 * @phydev: the PHY device
 * @id: the PHY ID to be matched
 *
 * Compare the @phydev clause 22 ID with the provided @id and return true or
 * false depending whether it matches, using the bound driver mask. The
 * @phydev must be bound to a driver.
 */
static inline bool phydev_id_compare(struct phy_device *phydev, u32 id)
{
	return phy_id_compare(id, phydev->phy_id, phydev->drv->phy_id_mask);
}

const char *phy_speed_to_str(int speed);
const char *phy_duplex_to_str(unsigned int duplex);
const char *phy_rate_matching_to_str(int rate_matching);

int phy_interface_num_ports(phy_interface_t interface);

/**
 * phy_is_started - Convenience function to check whether PHY is started
 * @phydev: The phy_device struct
 */
static inline bool phy_is_started(struct phy_device *phydev)
{
	return phydev->state >= PHY_UP;
}

/**
 * phy_driver_is_genphy - Convenience function to check whether PHY is driven
 *                        by one of the generic PHY drivers
 * @phydev: The phy_device struct
 * Return: true if PHY is driven by one of the genphy drivers
 */
static inline bool phy_driver_is_genphy(struct phy_device *phydev)
{
	return phydev->is_genphy_driven;
}

/**
 * phy_disable_eee_mode - Don't advertise an EEE mode.
 * @phydev: The phy_device struct
 * @link_mode: The EEE mode to be disabled
 */
static inline void phy_disable_eee_mode(struct phy_device *phydev, u32 link_mode)
{
	WARN_ON(phy_is_started(phydev));

	linkmode_set_bit(link_mode, phydev->eee_disabled_modes);
	linkmode_clear_bit(link_mode, phydev->advertising_eee);
}

void phy_resolve_aneg_pause(struct phy_device *phydev);
void phy_resolve_aneg_linkmode(struct phy_device *phydev);

/**
 * phy_read - Convenience function for reading a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to read
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
static inline int phy_read(struct phy_device *phydev, u32 regnum)
{
	return mdiobus_read(phydev->mdio.bus, phydev->mdio.addr, regnum);
}

#define phy_read_poll_timeout(phydev, regnum, val, cond, sleep_us, \
				timeout_us, sleep_before_read) \
({ \
	int __ret, __val; \
	__ret = read_poll_timeout(__val = phy_read, val, \
				  __val < 0 || (cond), \
		sleep_us, timeout_us, sleep_before_read, phydev, regnum); \
	if (__val < 0) \
		__ret = __val; \
	if (__ret) \
		phydev_err(phydev, "%s failed: %d\n", __func__, __ret); \
	__ret; \
})

/**
 * __phy_read - convenience function for reading a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to read
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_read(struct phy_device *phydev, u32 regnum)
{
	return __mdiobus_read(phydev->mdio.bus, phydev->mdio.addr, regnum);
}

/**
 * phy_write - Convenience function for writing a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
static inline int phy_write(struct phy_device *phydev, u32 regnum, u16 val)
{
	return mdiobus_write(phydev->mdio.bus, phydev->mdio.addr, regnum, val);
}

/**
 * __phy_write - Convenience function for writing a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_write(struct phy_device *phydev, u32 regnum, u16 val)
{
	return __mdiobus_write(phydev->mdio.bus, phydev->mdio.addr, regnum,
			       val);
}

/**
 * __phy_modify_changed() - Convenience function for modifying a PHY register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Unlocked helper function which allows a PHY register to be modified as
 * new register value = (old register value & ~mask) | set
 *
 * Returns negative errno, 0 if there was no change, and 1 in case of change
 */
static inline int __phy_modify_changed(struct phy_device *phydev, u32 regnum,
				       u16 mask, u16 set)
{
	return __mdiobus_modify_changed(phydev->mdio.bus, phydev->mdio.addr,
					regnum, mask, set);
}

/*
 * phy_read_mmd - Convenience function for reading a register
 * from an MMD on a given PHY.
 */
int phy_read_mmd(struct phy_device *phydev, int devad, u32 regnum);

/**
 * phy_read_mmd_poll_timeout - Periodically poll a PHY register until a
 *                             condition is met or a timeout occurs
 *
 * @phydev: The phy_device struct
 * @devaddr: The MMD to read from
 * @regnum: The register on the MMD to read
 * @val: Variable to read the register into
 * @cond: Break condition (usually involving @val)
 * @sleep_us: Maximum time to sleep between reads in us (0 tight-loops). Please
 *            read usleep_range() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 * @sleep_before_read: if it is true, sleep @sleep_us before read.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @args is stored in @val. Must not
 * be called from atomic context if sleep_us or timeout_us are used.
 */
#define phy_read_mmd_poll_timeout(phydev, devaddr, regnum, val, cond, \
				  sleep_us, timeout_us, sleep_before_read) \
({ \
	int __ret, __val; \
	__ret = read_poll_timeout(__val = phy_read_mmd, val, \
				  __val < 0 || (cond), \
				  sleep_us, timeout_us, sleep_before_read, \
				  phydev, devaddr, regnum); \
	if (__val < 0) \
		__ret = __val; \
	if (__ret) \
		phydev_err(phydev, "%s failed: %d\n", __func__, __ret); \
	__ret; \
})

/*
 * __phy_read_mmd - Convenience function for reading a register
 * from an MMD on a given PHY.
 */
int __phy_read_mmd(struct phy_device *phydev, int devad, u32 regnum);

/*
 * phy_write_mmd - Convenience function for writing a register
 * on an MMD on a given PHY.
 */
int phy_write_mmd(struct phy_device *phydev, int devad, u32 regnum, u16 val);

/*
 * __phy_write_mmd - Convenience function for writing a register
 * on an MMD on a given PHY.
 */
int __phy_write_mmd(struct phy_device *phydev, int devad, u32 regnum, u16 val);

int __phy_modify_changed(struct phy_device *phydev, u32 regnum, u16 mask,
			 u16 set);
int phy_modify_changed(struct phy_device *phydev, u32 regnum, u16 mask,
		       u16 set);
int __phy_modify(struct phy_device *phydev, u32 regnum, u16 mask, u16 set);
int phy_modify(struct phy_device *phydev, u32 regnum, u16 mask, u16 set);

int __phy_modify_mmd_changed(struct phy_device *phydev, int devad, u32 regnum,
			     u16 mask, u16 set);
int phy_modify_mmd_changed(struct phy_device *phydev, int devad, u32 regnum,
			   u16 mask, u16 set);
int __phy_modify_mmd(struct phy_device *phydev, int devad, u32 regnum,
		     u16 mask, u16 set);
int phy_modify_mmd(struct phy_device *phydev, int devad, u32 regnum,
		   u16 mask, u16 set);

/**
 * __phy_set_bits - Convenience function for setting bits in a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: bits to set
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_set_bits(struct phy_device *phydev, u32 regnum, u16 val)
{
	return __phy_modify(phydev, regnum, 0, val);
}

/**
 * __phy_clear_bits - Convenience function for clearing bits in a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: bits to clear
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_clear_bits(struct phy_device *phydev, u32 regnum,
				   u16 val)
{
	return __phy_modify(phydev, regnum, val, 0);
}

/**
 * phy_set_bits - Convenience function for setting bits in a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: bits to set
 */
static inline int phy_set_bits(struct phy_device *phydev, u32 regnum, u16 val)
{
	return phy_modify(phydev, regnum, 0, val);
}

/**
 * phy_clear_bits - Convenience function for clearing bits in a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: bits to clear
 */
static inline int phy_clear_bits(struct phy_device *phydev, u32 regnum, u16 val)
{
	return phy_modify(phydev, regnum, val, 0);
}

/**
 * __phy_set_bits_mmd - Convenience function for setting bits in a register
 * on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @val: bits to set
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_set_bits_mmd(struct phy_device *phydev, int devad,
		u32 regnum, u16 val)
{
	return __phy_modify_mmd(phydev, devad, regnum, 0, val);
}

/**
 * __phy_clear_bits_mmd - Convenience function for clearing bits in a register
 * on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @val: bits to clear
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_clear_bits_mmd(struct phy_device *phydev, int devad,
		u32 regnum, u16 val)
{
	return __phy_modify_mmd(phydev, devad, regnum, val, 0);
}

/**
 * phy_set_bits_mmd - Convenience function for setting bits in a register
 * on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @val: bits to set
 */
static inline int phy_set_bits_mmd(struct phy_device *phydev, int devad,
		u32 regnum, u16 val)
{
	return phy_modify_mmd(phydev, devad, regnum, 0, val);
}

/**
 * phy_clear_bits_mmd - Convenience function for clearing bits in a register
 * on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @val: bits to clear
 */
static inline int phy_clear_bits_mmd(struct phy_device *phydev, int devad,
		u32 regnum, u16 val)
{
	return phy_modify_mmd(phydev, devad, regnum, val, 0);
}

/**
 * phy_interrupt_is_valid - Convenience function for testing a given PHY irq
 * @phydev: the phy_device struct
 *
 * NOTE: must be kept in sync with addition/removal of PHY_POLL and
 * PHY_MAC_INTERRUPT
 */
static inline bool phy_interrupt_is_valid(struct phy_device *phydev)
{
	return phydev->irq != PHY_POLL && phydev->irq != PHY_MAC_INTERRUPT;
}

/**
 * phy_polling_mode - Convenience function for testing whether polling is
 * used to detect PHY status changes
 * @phydev: the phy_device struct
 */
static inline bool phy_polling_mode(struct phy_device *phydev)
{
	if (phydev->state == PHY_CABLETEST)
		if (phydev->drv->flags & PHY_POLL_CABLE_TEST)
			return true;

	if (phydev->drv->update_stats)
		return true;

	return phydev->irq == PHY_POLL;
}

/**
 * phy_has_hwtstamp - Tests whether a PHY time stamp configuration.
 * @phydev: the phy_device struct
 */
static inline bool phy_has_hwtstamp(struct phy_device *phydev)
{
	return phydev && phydev->mii_ts && phydev->mii_ts->hwtstamp;
}

/**
 * phy_has_rxtstamp - Tests whether a PHY supports receive time stamping.
 * @phydev: the phy_device struct
 */
static inline bool phy_has_rxtstamp(struct phy_device *phydev)
{
	return phydev && phydev->mii_ts && phydev->mii_ts->rxtstamp;
}

/**
 * phy_has_tsinfo - Tests whether a PHY reports time stamping and/or
 * PTP hardware clock capabilities.
 * @phydev: the phy_device struct
 */
static inline bool phy_has_tsinfo(struct phy_device *phydev)
{
	return phydev && phydev->mii_ts && phydev->mii_ts->ts_info;
}

/**
 * phy_has_txtstamp - Tests whether a PHY supports transmit time stamping.
 * @phydev: the phy_device struct
 */
static inline bool phy_has_txtstamp(struct phy_device *phydev)
{
	return phydev && phydev->mii_ts && phydev->mii_ts->txtstamp;
}

static inline int phy_hwtstamp(struct phy_device *phydev,
			       struct kernel_hwtstamp_config *cfg,
			       struct netlink_ext_ack *extack)
{
	return phydev->mii_ts->hwtstamp(phydev->mii_ts, cfg, extack);
}

static inline bool phy_rxtstamp(struct phy_device *phydev, struct sk_buff *skb,
				int type)
{
	return phydev->mii_ts->rxtstamp(phydev->mii_ts, skb, type);
}

static inline int phy_ts_info(struct phy_device *phydev,
			      struct kernel_ethtool_ts_info *tsinfo)
{
	return phydev->mii_ts->ts_info(phydev->mii_ts, tsinfo);
}

static inline void phy_txtstamp(struct phy_device *phydev, struct sk_buff *skb,
				int type)
{
	phydev->mii_ts->txtstamp(phydev->mii_ts, skb, type);
}

/**
 * phy_is_default_hwtstamp - Is the PHY hwtstamp the default timestamp
 * @phydev: Pointer to phy_device
 *
 * This is used to get default timestamping device taking into account
 * the new API choice, which is selecting the timestamping from MAC by
 * default if the phydev does not have default_timestamp flag enabled.
 *
 * Return: True if phy is the default hw timestamp, false otherwise.
 */
static inline bool phy_is_default_hwtstamp(struct phy_device *phydev)
{
	return phy_has_hwtstamp(phydev) && phydev->default_timestamp;
}

/**
 * phy_on_sfp - Convenience function for testing if a PHY is on an SFP module
 * @phydev: the phy_device struct
 */
static inline bool phy_on_sfp(struct phy_device *phydev)
{
	return phydev->is_on_sfp_module;
}

/**
 * phy_interface_mode_is_rgmii - Convenience function for testing if a
 * PHY interface mode is RGMII (all variants)
 * @mode: the &phy_interface_t enum
 */
static inline bool phy_interface_mode_is_rgmii(phy_interface_t mode)
{
	return mode >= PHY_INTERFACE_MODE_RGMII &&
		mode <= PHY_INTERFACE_MODE_RGMII_TXID;
};

/**
 * phy_interface_mode_is_8023z() - does the PHY interface mode use 802.3z
 *   negotiation
 * @mode: one of &enum phy_interface_t
 *
 * Returns true if the PHY interface mode uses the 16-bit negotiation
 * word as defined in 802.3z. (See 802.3-2015 37.2.1 Config_Reg encoding)
 */
static inline bool phy_interface_mode_is_8023z(phy_interface_t mode)
{
	return mode == PHY_INTERFACE_MODE_1000BASEX ||
	       mode == PHY_INTERFACE_MODE_2500BASEX;
}

/**
 * phy_interface_is_rgmii - Convenience function for testing if a PHY interface
 * is RGMII (all variants)
 * @phydev: the phy_device struct
 */
static inline bool phy_interface_is_rgmii(struct phy_device *phydev)
{
	return phy_interface_mode_is_rgmii(phydev->interface);
};

/**
 * phy_is_pseudo_fixed_link - Convenience function for testing if this
 * PHY is the CPU port facing side of an Ethernet switch, or similar.
 * @phydev: the phy_device struct
 */
static inline bool phy_is_pseudo_fixed_link(struct phy_device *phydev)
{
	return phydev->is_pseudo_fixed_link;
}

int phy_save_page(struct phy_device *phydev);
int phy_select_page(struct phy_device *phydev, int page);
int phy_restore_page(struct phy_device *phydev, int oldpage, int ret);
int phy_read_paged(struct phy_device *phydev, int page, u32 regnum);
int phy_write_paged(struct phy_device *phydev, int page, u32 regnum, u16 val);
int phy_modify_paged_changed(struct phy_device *phydev, int page, u32 regnum,
			     u16 mask, u16 set);
int phy_modify_paged(struct phy_device *phydev, int page, u32 regnum,
		     u16 mask, u16 set);

struct phy_device *phy_device_create(struct mii_bus *bus, int addr, u32 phy_id,
				     bool is_c45,
				     struct phy_c45_device_ids *c45_ids);
int fwnode_get_phy_id(struct fwnode_handle *fwnode, u32 *phy_id);
struct mdio_device *fwnode_mdio_find_device(struct fwnode_handle *fwnode);
struct phy_device *fwnode_phy_find_device(struct fwnode_handle *phy_fwnode);
struct fwnode_handle *fwnode_get_phy_node(const struct fwnode_handle *fwnode);
struct phy_device *get_phy_device(struct mii_bus *bus, int addr, bool is_c45);
int phy_device_register(struct phy_device *phy);
void phy_device_free(struct phy_device *phydev);
void phy_device_remove(struct phy_device *phydev);
int phy_get_c45_ids(struct phy_device *phydev);
int phy_init_hw(struct phy_device *phydev);
int phy_suspend(struct phy_device *phydev);
int phy_resume(struct phy_device *phydev);
int __phy_resume(struct phy_device *phydev);
int phy_loopback(struct phy_device *phydev, bool enable, int speed);
int phy_sfp_connect_phy(void *upstream, struct phy_device *phy);
void phy_sfp_disconnect_phy(void *upstream, struct phy_device *phy);
void phy_sfp_attach(void *upstream, struct sfp_bus *bus);
void phy_sfp_detach(void *upstream, struct sfp_bus *bus);
int phy_sfp_probe(struct phy_device *phydev,
	          const struct sfp_upstream_ops *ops);
struct phy_device *phy_attach(struct net_device *dev, const char *bus_id,
			      phy_interface_t interface);
struct phy_device *phy_find_first(struct mii_bus *bus);
int phy_attach_direct(struct net_device *dev, struct phy_device *phydev,
		      u32 flags, phy_interface_t interface);
int phy_connect_direct(struct net_device *dev, struct phy_device *phydev,
		       void (*handler)(struct net_device *),
		       phy_interface_t interface);
struct phy_device *phy_connect(struct net_device *dev, const char *bus_id,
			       void (*handler)(struct net_device *),
			       phy_interface_t interface);
void phy_disconnect(struct phy_device *phydev);
void phy_detach(struct phy_device *phydev);
void phy_start(struct phy_device *phydev);
void phy_stop(struct phy_device *phydev);
int phy_config_aneg(struct phy_device *phydev);
int _phy_start_aneg(struct phy_device *phydev);
int phy_start_aneg(struct phy_device *phydev);
int phy_aneg_done(struct phy_device *phydev);
unsigned int phy_inband_caps(struct phy_device *phydev,
			     phy_interface_t interface);
int phy_config_inband(struct phy_device *phydev, unsigned int modes);
int phy_speed_down(struct phy_device *phydev, bool sync);
int phy_speed_up(struct phy_device *phydev);
bool phy_check_valid(int speed, int duplex, unsigned long *features);

int phy_restart_aneg(struct phy_device *phydev);
int phy_reset_after_clk_enable(struct phy_device *phydev);

#if IS_ENABLED(CONFIG_PHYLIB)
int phy_start_cable_test(struct phy_device *phydev,
			 struct netlink_ext_ack *extack);
int phy_start_cable_test_tdr(struct phy_device *phydev,
			     struct netlink_ext_ack *extack,
			     const struct phy_tdr_config *config);
#else
static inline
int phy_start_cable_test(struct phy_device *phydev,
			 struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG(extack, "Kernel not compiled with PHYLIB support");
	return -EOPNOTSUPP;
}
static inline
int phy_start_cable_test_tdr(struct phy_device *phydev,
			     struct netlink_ext_ack *extack,
			     const struct phy_tdr_config *config)
{
	NL_SET_ERR_MSG(extack, "Kernel not compiled with PHYLIB support");
	return -EOPNOTSUPP;
}
#endif

static inline void phy_device_reset(struct phy_device *phydev, int value)
{
	mdio_device_reset(&phydev->mdio, value);
}

#define phydev_err(_phydev, format, args...)	\
	dev_err(&_phydev->mdio.dev, format, ##args)

#define phydev_err_probe(_phydev, err, format, args...)	\
	dev_err_probe(&_phydev->mdio.dev, err, format, ##args)

#define phydev_info(_phydev, format, args...)	\
	dev_info(&_phydev->mdio.dev, format, ##args)

#define phydev_warn(_phydev, format, args...)	\
	dev_warn(&_phydev->mdio.dev, format, ##args)

#define phydev_dbg(_phydev, format, args...)	\
	dev_dbg(&_phydev->mdio.dev, format, ##args)

static inline const char *phydev_name(const struct phy_device *phydev)
{
	return dev_name(&phydev->mdio.dev);
}

static inline void phy_lock_mdio_bus(struct phy_device *phydev)
{
	mutex_lock(&phydev->mdio.bus->mdio_lock);
}

static inline void phy_unlock_mdio_bus(struct phy_device *phydev)
{
	mutex_unlock(&phydev->mdio.bus->mdio_lock);
}

void phy_attached_print(struct phy_device *phydev, const char *fmt, ...)
	__printf(2, 3);
char *phy_attached_info_irq(struct phy_device *phydev)
	__malloc;
void phy_attached_info(struct phy_device *phydev);

int genphy_match_phy_device(struct phy_device *phydev,
			    const struct phy_driver *phydrv);

/* Clause 22 PHY */
int genphy_read_abilities(struct phy_device *phydev);
int genphy_setup_forced(struct phy_device *phydev);
int genphy_restart_aneg(struct phy_device *phydev);
int genphy_check_and_restart_aneg(struct phy_device *phydev, bool restart);
int __genphy_config_aneg(struct phy_device *phydev, bool changed);
int genphy_aneg_done(struct phy_device *phydev);
int genphy_update_link(struct phy_device *phydev);
int genphy_read_lpa(struct phy_device *phydev);
int genphy_read_status_fixed(struct phy_device *phydev);
int genphy_read_status(struct phy_device *phydev);
int genphy_read_master_slave(struct phy_device *phydev);
int genphy_suspend(struct phy_device *phydev);
int genphy_resume(struct phy_device *phydev);
int genphy_loopback(struct phy_device *phydev, bool enable, int speed);
int genphy_soft_reset(struct phy_device *phydev);
irqreturn_t genphy_handle_interrupt_no_ack(struct phy_device *phydev);

static inline int genphy_config_aneg(struct phy_device *phydev)
{
	return __genphy_config_aneg(phydev, false);
}

static inline int genphy_no_config_intr(struct phy_device *phydev)
{
	return 0;
}
int genphy_read_mmd_unsupported(struct phy_device *phdev, int devad,
				u16 regnum);
int genphy_write_mmd_unsupported(struct phy_device *phdev, int devnum,
				 u16 regnum, u16 val);

/* Clause 37 */
int genphy_c37_config_aneg(struct phy_device *phydev);
int genphy_c37_read_status(struct phy_device *phydev, bool *changed);

/* Clause 45 PHY */
int genphy_c45_restart_aneg(struct phy_device *phydev);
int genphy_c45_check_and_restart_aneg(struct phy_device *phydev, bool restart);
int genphy_c45_aneg_done(struct phy_device *phydev);
int genphy_c45_read_link(struct phy_device *phydev);
int genphy_c45_read_lpa(struct phy_device *phydev);
int genphy_c45_read_pma(struct phy_device *phydev);
int genphy_c45_pma_setup_forced(struct phy_device *phydev);
int genphy_c45_pma_baset1_setup_master_slave(struct phy_device *phydev);
int genphy_c45_an_config_aneg(struct phy_device *phydev);
int genphy_c45_an_disable_aneg(struct phy_device *phydev);
int genphy_c45_read_mdix(struct phy_device *phydev);
int genphy_c45_pma_read_abilities(struct phy_device *phydev);
int genphy_c45_pma_read_ext_abilities(struct phy_device *phydev);
int genphy_c45_pma_baset1_read_abilities(struct phy_device *phydev);
int genphy_c45_read_eee_abilities(struct phy_device *phydev);
int genphy_c45_pma_baset1_read_master_slave(struct phy_device *phydev);
int genphy_c45_read_status(struct phy_device *phydev);
int genphy_c45_baset1_read_status(struct phy_device *phydev);
int genphy_c45_config_aneg(struct phy_device *phydev);
int genphy_c45_loopback(struct phy_device *phydev, bool enable, int speed);
int genphy_c45_pma_resume(struct phy_device *phydev);
int genphy_c45_pma_suspend(struct phy_device *phydev);
int genphy_c45_fast_retrain(struct phy_device *phydev, bool enable);
int genphy_c45_plca_get_cfg(struct phy_device *phydev,
			    struct phy_plca_cfg *plca_cfg);
int genphy_c45_plca_set_cfg(struct phy_device *phydev,
			    const struct phy_plca_cfg *plca_cfg);
int genphy_c45_plca_get_status(struct phy_device *phydev,
			       struct phy_plca_status *plca_st);
int genphy_c45_eee_is_active(struct phy_device *phydev, unsigned long *lp);
int genphy_c45_ethtool_get_eee(struct phy_device *phydev,
			       struct ethtool_keee *data);
int genphy_c45_ethtool_set_eee(struct phy_device *phydev,
			       struct ethtool_keee *data);
int genphy_c45_an_config_eee_aneg(struct phy_device *phydev);

/* The gen10g_* functions are the old Clause 45 stub */
int gen10g_config_aneg(struct phy_device *phydev);

static inline int phy_read_status(struct phy_device *phydev)
{
	if (!phydev->drv)
		return -EIO;

	if (phydev->drv->read_status)
		return phydev->drv->read_status(phydev);
	else
		return genphy_read_status(phydev);
}

void phy_driver_unregister(struct phy_driver *drv);
void phy_drivers_unregister(struct phy_driver *drv, int n);
int phy_driver_register(struct phy_driver *new_driver, struct module *owner);
int phy_drivers_register(struct phy_driver *new_driver, int n,
			 struct module *owner);
void phy_error(struct phy_device *phydev);
void phy_state_machine(struct work_struct *work);
void phy_trigger_machine(struct phy_device *phydev);
void phy_mac_interrupt(struct phy_device *phydev);
void phy_start_machine(struct phy_device *phydev);
void phy_stop_machine(struct phy_device *phydev);
void phy_ethtool_ksettings_get(struct phy_device *phydev,
			       struct ethtool_link_ksettings *cmd);
int phy_ethtool_ksettings_set(struct phy_device *phydev,
			      const struct ethtool_link_ksettings *cmd);
int phy_mii_ioctl(struct phy_device *phydev, struct ifreq *ifr, int cmd);
int phy_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
int phy_do_ioctl_running(struct net_device *dev, struct ifreq *ifr, int cmd);
int phy_disable_interrupts(struct phy_device *phydev);
void phy_request_interrupt(struct phy_device *phydev);
void phy_free_interrupt(struct phy_device *phydev);
void phy_print_status(struct phy_device *phydev);
int phy_get_rate_matching(struct phy_device *phydev,
			    phy_interface_t iface);
void phy_set_max_speed(struct phy_device *phydev, u32 max_speed);
void phy_remove_link_mode(struct phy_device *phydev, u32 link_mode);
void phy_advertise_supported(struct phy_device *phydev);
void phy_advertise_eee_all(struct phy_device *phydev);
void phy_support_sym_pause(struct phy_device *phydev);
void phy_support_asym_pause(struct phy_device *phydev);
void phy_support_eee(struct phy_device *phydev);
void phy_disable_eee(struct phy_device *phydev);
void phy_set_sym_pause(struct phy_device *phydev, bool rx, bool tx,
		       bool autoneg);
void phy_set_asym_pause(struct phy_device *phydev, bool rx, bool tx);
bool phy_validate_pause(struct phy_device *phydev,
			struct ethtool_pauseparam *pp);
void phy_get_pause(struct phy_device *phydev, bool *tx_pause, bool *rx_pause);

s32 phy_get_internal_delay(struct phy_device *phydev, const int *delay_values,
			   int size, bool is_rx);

int phy_get_tx_amplitude_gain(struct phy_device *phydev, struct device *dev,
			      enum ethtool_link_mode_bit_indices linkmode,
			      u32 *val);

int phy_get_mac_termination(struct phy_device *phydev, struct device *dev,
			    u32 *val);

void phy_resolve_pause(unsigned long *local_adv, unsigned long *partner_adv,
		       bool *tx_pause, bool *rx_pause);

int phy_register_fixup_for_id(const char *bus_id,
			      int (*run)(struct phy_device *));
int phy_register_fixup_for_uid(u32 phy_uid, u32 phy_uid_mask,
			       int (*run)(struct phy_device *));

int phy_unregister_fixup(const char *bus_id, u32 phy_uid, u32 phy_uid_mask);
int phy_unregister_fixup_for_id(const char *bus_id);
int phy_unregister_fixup_for_uid(u32 phy_uid, u32 phy_uid_mask);

int phy_eee_tx_clock_stop_capable(struct phy_device *phydev);
int phy_eee_rx_clock_stop(struct phy_device *phydev, bool clk_stop_enable);
int phy_init_eee(struct phy_device *phydev, bool clk_stop_enable);
int phy_get_eee_err(struct phy_device *phydev);
int phy_ethtool_set_eee(struct phy_device *phydev, struct ethtool_keee *data);
int phy_ethtool_get_eee(struct phy_device *phydev, struct ethtool_keee *data);
int phy_ethtool_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol);
void phy_ethtool_get_wol(struct phy_device *phydev,
			 struct ethtool_wolinfo *wol);
int phy_ethtool_get_link_ksettings(struct net_device *ndev,
				   struct ethtool_link_ksettings *cmd);
int phy_ethtool_set_link_ksettings(struct net_device *ndev,
				   const struct ethtool_link_ksettings *cmd);
int phy_ethtool_nway_reset(struct net_device *ndev);

int phy_ethtool_get_strings(struct phy_device *phydev, u8 *data);
int phy_ethtool_get_sset_count(struct phy_device *phydev);
int phy_ethtool_get_stats(struct phy_device *phydev,
			  struct ethtool_stats *stats, u64 *data);

void __phy_ethtool_get_phy_stats(struct phy_device *phydev,
			 struct ethtool_eth_phy_stats *phy_stats,
			 struct ethtool_phy_stats *phydev_stats);
void __phy_ethtool_get_link_ext_stats(struct phy_device *phydev,
				      struct ethtool_link_ext_stats *link_stats);

int phy_ethtool_get_plca_cfg(struct phy_device *phydev,
			     struct phy_plca_cfg *plca_cfg);
int phy_ethtool_set_plca_cfg(struct phy_device *phydev,
			     const struct phy_plca_cfg *plca_cfg,
			     struct netlink_ext_ack *extack);
int phy_ethtool_get_plca_status(struct phy_device *phydev,
				struct phy_plca_status *plca_st);

int __phy_hwtstamp_get(struct phy_device *phydev,
		       struct kernel_hwtstamp_config *config);
int __phy_hwtstamp_set(struct phy_device *phydev,
		       struct kernel_hwtstamp_config *config,
		       struct netlink_ext_ack *extack);

extern const struct bus_type mdio_bus_type;
extern const struct class mdio_bus_class;

struct mdio_board_info {
	const char	*bus_id;
	char		modalias[MDIO_NAME_SIZE];
	int		mdio_addr;
	const void	*platform_data;
};

int mdiobus_register_board_info(const struct mdio_board_info *info,
				unsigned int n);

/**
 * phy_module_driver() - Helper macro for registering PHY drivers
 * @__phy_drivers: array of PHY drivers to register
 * @__count: Numbers of members in array
 *
 * Helper macro for PHY drivers which do not do anything special in module
 * init/exit. Each module may only use this macro once, and calling it
 * replaces module_init() and module_exit().
 */
#define phy_module_driver(__phy_drivers, __count)			\
static int __init phy_module_init(void)					\
{									\
	return phy_drivers_register(__phy_drivers, __count, THIS_MODULE); \
}									\
module_init(phy_module_init);						\
static void __exit phy_module_exit(void)				\
{									\
	phy_drivers_unregister(__phy_drivers, __count);			\
}									\
module_exit(phy_module_exit)

#define module_phy_driver(__phy_drivers)				\
	phy_module_driver(__phy_drivers, ARRAY_SIZE(__phy_drivers))

#endif /* __PHY_H */
