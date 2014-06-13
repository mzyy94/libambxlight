#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/uaccess.h>


#define VID_AMBXLIGHT 0x06a3
#define PID_AMBXLIGHT 0x0dc5

static void* usbtest_probe(struct usb_device *dev, unsigned int ifnum, const struct usb_device_id *id)
{
	struct usb_interface_descriptor *inf;
	struct usb_config_descriptor *config;
	unsigned int pipe;
	int n, intr_interval;
	if ((dev->descriptor.idVendor != VID_AMBXLIGHT) ||
			(dev->descriptor.idProduct != PID_AMBXLIGHT))
	{
		return NULL;
	}
	if (g_usbtest_data) {
		printk(KERN_INFO "USBTEST: no more probe.\n");
		return NULL;
	}
	config = dev->actconfig;
	inf = &config->interface[ifnum].altsetting[0];
	g_usbtest_data = kmalloc(sizeof(usbtest_data_t), GFP_KERNEL);
	if (g_usbtest_data == NULL) {
		printk(KERN_INFO "no memory¥n");
		return NULL;
	}
	intr_interval = 0;
	g_usbtest_data->ep_intr_in = -1;
	g_usbtest_data->ep_bulk_out = -1;
	g_usbtest_data->ep_bulk_in = -1;
	for(n = 0; n < inf->bNumEndpoints; n++) {
		switch(inf->endpoint[n].bEndpointAddress&USB_ENDPOINT_NUMBER_MASK) {
			case 1:
				if (! (inf->endpoint[n].bEndpointAddress&USB_ENDPOINT_DIR_MASK))
					break;
				if ((inf->endpoint[n].bmAttributes&USB_ENDPOINT_XFERTYPE_MASK) ==
						USB_ENDPOINT_XFER_INT) {
					g_usbtest_data->ep_intr_in = 1;
					intr_interval = inf->endpoint[n].bInterval;
				}
				break;
			case 2:
				if (inf->endpoint[n].bEndpointAddress&USB_ENDPOINT_DIR_MASK)
					break;
				if ((inf->endpoint[n].bmAttributes&USB_ENDPOINT_XFER_INT) ==
						USB_ENDPOINT_XFER_BULK) {
					g_usbtest_data->ep_bulk_out = 2;
				}
				break;
			case 5:
				if (! (inf->endpoint[n].bEndpointAddress&USB_ENDPOINT_DIR_MASK))
					break;
				if ((inf->endpoint[n].bmAttributes&USB_ENDPOINT_XFER_INT) ==
						USB_ENDPOINT_XFER_BULK) {
					g_usbtest_data->ep_bulk_in = 5;
				}
				break;
		}
	}
	if (g_usbtest_data->ep_intr_in == -1) {
		printk("USBTEST:1: endpoint error!!\n");
		return NULL;
	}
	if (g_usbtest_data->ep_bulk_out == -1) {
		printk("USBTEST:2: endpoint error!!\n");
		return NULL;
	}
	if (g_usbtest_data->ep_bulk_in == -1) {
		printk("USBTEST:5: endpoint error!!\n");
		return NULL;
	}
	g_usbtest_data->dev = dev;
	g_usbtest_data->isopen = 0;
	pipe = usb_rcvintpipe(g_usbtest_data->dev, g_usbtest_data->ep_intr_in);
	FILL_INT_URB(&g_usbtest_data->irq, dev, pipe, g_usbtest_data->data, 2,
			usbtest_irq, g_usbtest_data, intr_interval);
	return g_usbtest_data;
}
