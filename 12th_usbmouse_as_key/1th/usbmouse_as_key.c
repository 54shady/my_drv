/*
 * ≤Œøº drivers\hid\usbhid\usbmouse.c
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

#define DRIVER_VERSION "usbmouse_as_key_v0.1"
#define DRIVER_DESC "usbmouse_as_key"

static struct usb_device_id usbmouse_as_key_id_table [] = {
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};

static int usbmouse_as_key_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	printk("usbmouse_as_key_probe is being called\n");
	
	return 0;
}

static void usbmouse_as_key_disconnect(struct usb_interface *intf)
{
	printk("usbmouse_as_key_disconnect is being called\n");
}

/* 1. ∑÷≈‰/…Ë÷√ usb_device */
static struct usb_driver usbmouse_as_key_driver = {
	.name		= "usbmouse_as_key",
	.probe		= usbmouse_as_key_probe,
	.disconnect	= usbmouse_as_key_disconnect,
	.id_table	= usbmouse_as_key_id_table,
};

static int usbmouse_as_key_init(void)
{
	/* 2.◊¢≤·usb_device */
	int retval = usb_register(&usbmouse_as_key_driver);
	if (retval == 0)
		info(DRIVER_VERSION ":" DRIVER_DESC);

	return retval;
}

static void usbmouse_as_key_exit(void)
{
	usb_deregister(&usbmouse_as_key_driver);
}

module_init(usbmouse_as_key_init);
module_exit(usbmouse_as_key_exit);
MODULE_LICENSE("GPL");
