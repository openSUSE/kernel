/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 * (C) Copyright 2003-2005 MontaVista Software Inc.
 * 
 * Bus Glue for PPC On-Chip OHCI driver
 * Tested on Freescale MPC5200 and IBM STB04xxx
 *
 * Modified by Dale Farnsworth <dale@farnsworth.org> from ohci-sa1111.c
 *
 * This file is licenced under the GPL.
 */

#include <asm/usb.h>

static void usb_hcd_ppc_soc_remove(struct usb_hcd *, struct platform_device *);

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_ppc_soc_probe - initialize On-Chip HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 * Store this function in the HCD's struct pci_driver as probe().
 */
static int usb_hcd_ppc_soc_probe(const struct hc_driver *driver,
			  struct usb_hcd **hcd_out,
			  struct platform_device *pdev)
{
	int retval;
	struct usb_hcd *hcd = 0;
	struct ohci_hcd	*ohci;
	struct resource *res;
	int irq;
	struct usb_hcd_platform_data *pd = pdev->dev.platform_data;

	pr_debug("initializing PPC-SOC USB Controller\n");

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		pr_debug(__FILE__ ": no irq\n");
		return -ENODEV;
	}
	irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_debug(__FILE__ ": no reg addr\n");
		return -ENODEV;
	}
	if (!request_mem_region(res->start, res->end - res->start + 1,
					hcd_name)) {
		pr_debug(__FILE__ ": request_mem_region failed\n");
		return -EBUSY;
	}

	if (pd->start && (retval = pd->start(pdev)))
		goto err0;

	hcd = usb_create_hcd(driver);
	if (!hcd){
		pr_debug(__FILE__ ": hcd_alloc failed\n");
		retval = -ENOMEM;
		goto err1;
	}

	ohci = hcd_to_ohci(hcd);

	ohci->flags |= OHCI_BIG_ENDIAN;

	ohci_hcd_init(ohci);

	hcd->irq = irq;
	hcd->regs = (struct ohci_regs *) ioremap(res->start,
						res->end - res->start + 1);
	if (!hcd->regs) {
		pr_debug(__FILE__ ": ioremap failed\n");
		retval = -ENOMEM;
		goto err2;
	}

	hcd->self.controller = &pdev->dev;

	retval = hcd_buffer_create(hcd);
	if (retval) {
		pr_debug(__FILE__ ": pool alloc fail\n");
		goto err3;
	}

	retval = request_irq(hcd->irq, usb_hcd_irq, SA_INTERRUPT,
				hcd_name, hcd);
	if (retval) {
		pr_debug(__FILE__ ": request_irq failed, returned %d\n",
								retval);
		retval = -EBUSY;
		goto err4;
	}

	info("%s (PPC-SOC) at 0x%p, irq %d\n",
	      hcd_name, hcd->regs, hcd->irq);

	hcd->self.bus_name = "PPC-SOC USB";

	usb_register_bus(&hcd->self);

	if ((retval = driver->start(hcd)) < 0) {
		usb_hcd_ppc_soc_remove(hcd, pdev);
		return retval;
	}

	*hcd_out = hcd;
	return 0;

 err4:
	hcd_buffer_destroy(hcd);
 err3:
	iounmap(hcd->regs);
 err2:
	dev_set_drvdata(&pdev->dev, NULL);
 	usb_put_hcd(hcd);
 err1:
	pr_debug("Removing PPC-SOC USB Controller\n");
	if (pd && pd->stop)
		pd->stop(pdev);
 err0:
	release_mem_region(res->start, res->end - res->start + 1);
	return retval;
}


/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_ppc_soc_remove - shutdown processing for On-Chip HCDs
 * @pdev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_ppc_soc_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
static void usb_hcd_ppc_soc_remove(struct usb_hcd *hcd, struct platform_device *pdev)
{
	struct resource *res;
	struct usb_hcd_platform_data *pd = pdev->dev.platform_data;

	pr_debug(__FILE__ ": remove: %s, state %x\n", hcd->self.bus_name,
								hcd->state);
	if (in_interrupt())
		BUG();

	hcd->state = USB_STATE_QUIESCING;

	pr_debug("%s: roothub graceful disconnect\n", hcd->self.bus_name);
	usb_disconnect(&hcd->self.root_hub);

	hcd->driver->stop(hcd);
	hcd->state = USB_STATE_HALT;

	free_irq(hcd->irq, hcd);
	hcd_buffer_destroy(hcd);

	usb_deregister_bus(&hcd->self);

	iounmap(hcd->regs);
	kfree(hcd);

	pr_debug("stopping PPC-SOC USB Controller\n");

	if (pd && pd->stop)
		pd->stop(pdev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, res->end - res->start + 1);
}

static int __devinit
ohci_ppc_soc_start(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	int		ret;

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run(ohci)) < 0) {
		err("can't start %s", ohci_to_hcd(ohci)->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver ohci_ppc_soc_hc_driver = {
	.description =		hcd_name,
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_ppc_soc_start,
	.stop =			ohci_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef	CONFIG_USB_SUSPEND
	.hub_suspend =		ohci_hub_suspend,
	.hub_resume =		ohci_hub_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

static int ohci_hcd_ppc_soc_drv_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = NULL;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	ret = usb_hcd_ppc_soc_probe(&ohci_ppc_soc_hc_driver, &hcd, pdev);

	if (ret == 0)
		dev_set_drvdata(dev, hcd);

	return ret;
}

static int ohci_hcd_ppc_soc_drv_remove(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	usb_hcd_ppc_soc_remove(hcd, pdev);

	dev_set_drvdata(dev, NULL);
	return 0;
}

static struct device_driver ohci_hcd_ppc_soc_driver = {
	.name		= "ppc-soc-ohci",
	.bus		= &platform_bus_type,
	.probe		= ohci_hcd_ppc_soc_drv_probe,
	.remove		= ohci_hcd_ppc_soc_drv_remove,
#if	defined(CONFIG_USB_SUSPEND) || defined(CONFIG_PM)
	/*.suspend	= ohci_hcd_ppc_soc_drv_suspend,*/
	/*.resume	= ohci_hcd_ppc_soc_drv_resume,*/
#endif
};

static int __init ohci_hcd_ppc_soc_init(void)
{
	pr_debug(DRIVER_INFO " (PPC SOC)\n");
	pr_debug("block sizes: ed %d td %d\n", sizeof(struct ed),
							sizeof(struct td));

	return driver_register(&ohci_hcd_ppc_soc_driver);
}

static void __exit ohci_hcd_ppc_soc_cleanup(void)
{
	driver_unregister(&ohci_hcd_ppc_soc_driver);
}

module_init(ohci_hcd_ppc_soc_init);
module_exit(ohci_hcd_ppc_soc_cleanup);
