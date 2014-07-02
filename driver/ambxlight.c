/*
 * Cyborg amBX Light Pods USB driver - 0.1
 *
 * Copyright (C) 2014 Yuki Mizuno (u@mzyy94.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>


/* Define these values to match your devices */
#define CYBORG_AMBX_LIGHT_VENDOR_ID	0x06a3
#define CYBORG_AMBX_LIGHT_PRODUCT_ID	0x0dc5

/* table of devices that work with this driver */
static const struct usb_device_id ambx_light_table[] = {
	{ USB_DEVICE(CYBORG_AMBX_LIGHT_VENDOR_ID, CYBORG_AMBX_LIGHT_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, ambx_light_table);


/* Get a minor range for your devices from the usb maintainer */
#define CYBORG_AMBX_LIGHT_MINOR_BASE	192

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER		(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */

/* Structure to hold all of our device specific stuff */
struct usb_ambx_light {
	struct usb_device	*udev;			/* the usb device for this device */
	struct usb_interface	*interface;		/* the interface for this device */
	struct semaphore	limit_sem;		/* limiting the number of writes in progress */
	struct usb_anchor	submitted;		/* in case we need to retract our submissions */
	unsigned char			*ctrl_buffer;	/* the buffer to send/receive data */
	struct urb		*ctrl_urb;			/* the urb to write/read data with */
	struct usb_ctrlrequest	*ctrl_dr;	/* setup packet information */
	size_t			ctrl_size;		/* the size of the send/receive buffer */
	__u8			ctrl_endpointAddr;	/* the address of the ctrl endpoint */
	int			errors;			/* the last request tanked */
	bool			ongoing_read;		/* a read is going on */
	spinlock_t		err_lock;		/* lock for errors */
	struct kref		kref;
	struct mutex		io_mutex;		/* synchronize I/O with disconnect */
};
#define to_ambx_light_dev(d) container_of(d, struct usb_ambx_light, kref)

static struct usb_driver ambx_light_driver;
static void ambx_light_draw_down(struct usb_ambx_light *dev);

static void ambx_light_delete(struct kref *kref)
{
	struct usb_ambx_light *dev = to_ambx_light_dev(kref);

	usb_free_urb(dev->ctrl_urb);
	usb_put_dev(dev->udev);
	kfree(dev->ctrl_buffer);
	kfree(dev->ctrl_dr);
	kfree(dev);
}

static int ambx_light_open(struct inode *inode, struct file *file)
{
	struct usb_ambx_light *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&ambx_light_driver, subminor);
	if (!interface) {
		pr_err("%s - error, can't find device for minor %d\n",
			__func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	retval = usb_autopm_get_interface(interface);
	if (retval)
		goto exit;

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* save our object in the file's private structure */
	file->private_data = dev;

exit:
	return retval;
}

static int ambx_light_release(struct inode *inode, struct file *file)
{
	struct usb_ambx_light *dev;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* allow the device to be autosuspended */
	mutex_lock(&dev->io_mutex);
	if (dev->interface)
		usb_autopm_put_interface(dev->interface);
	mutex_unlock(&dev->io_mutex);

	/* decrement the count on our device */
	kref_put(&dev->kref, ambx_light_delete);
	return 0;
}

static int ambx_light_flush(struct file *file, fl_owner_t id)
{
	struct usb_ambx_light *dev;
	int res;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* wait for io to stop */
	mutex_lock(&dev->io_mutex);
	ambx_light_draw_down(dev);

	/* read out errors, leave subsequent opens a clean slate */
	spin_lock_irq(&dev->err_lock);
	res = dev->errors ? (dev->errors == -EPIPE ? -EPIPE : -EIO) : 0;
	dev->errors = 0;
	spin_unlock_irq(&dev->err_lock);

	mutex_unlock(&dev->io_mutex);

	return res;
}

static void ambx_light_write_ctrl_callback(struct urb *urb)
{
	struct usb_ambx_light *dev;

	dev = urb->context;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			dev_err(&dev->interface->dev,
				"%s - nonzero write ctrl status received: %d\n",
				__func__, urb->status);

		spin_lock(&dev->err_lock);
		dev->errors = urb->status;
		spin_unlock(&dev->err_lock);
	}

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
	up(&dev->limit_sem);
}

static ssize_t ambx_light_write(struct file *file, const char *user_buffer,
			  size_t count, loff_t *ppos)
{
	struct usb_ambx_light *dev;
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	size_t writesize = min(count, (size_t)MAX_TRANSFER);

	dev = file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	/*
	 * limit the number of URBs in flight to stop a user from using up all
	 * RAM
	 */
	if (!(file->f_flags & O_NONBLOCK)) {
		if (down_interruptible(&dev->limit_sem)) {
			retval = -ERESTARTSYS;
			goto exit;
		}
	} else {
		if (down_trylock(&dev->limit_sem)) {
			retval = -EAGAIN;
			goto exit;
		}
	}

	spin_lock_irq(&dev->err_lock);
	retval = dev->errors;
	if (retval < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		retval = (retval == -EPIPE) ? retval : -EIO;
	}
	spin_unlock_irq(&dev->err_lock);
	if (retval < 0)
		goto error;

	/* create a urb, and a buffer for it, and copy the data to the urb */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		retval = -ENOMEM;
		goto error;
	}

	buf = usb_alloc_coherent(dev->udev, writesize, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!buf) {
		retval = -ENOMEM;
		goto error;
	}

	if (copy_from_user(buf, user_buffer, writesize)) {
		retval = -EFAULT;
		goto error;
	}

	/* check the data have correct format */
	if ((buf[0] & 0xff) == 0xa2) {
		if (writesize != 9) {
			retval = -EFAULT;
			goto error;
		}
	} else if((buf[0] & 0xff) == 0xa1) {
		if (writesize != 3) {
			retval = -EFAULT;
			goto error;
		}
	} else {
		retval = -EFAULT;
		goto error;
	}

	/* this lock makes sure we don't submit URBs to gone devices */
	mutex_lock(&dev->io_mutex);
	if (!dev->interface) {		/* disconnect() was called */
		mutex_unlock(&dev->io_mutex);
		retval = -ENODEV;
		goto error;
	}

	/* initialize the urb properly */
	dev->ctrl_dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!dev->ctrl_dr) {
		retval = -ENOMEM;
		goto error;
	}
	dev->ctrl_dr->bRequestType = 0x21;
	dev->ctrl_dr->bRequest = 0x09;
	dev->ctrl_dr->wValue = (buf[0] & 0xff);
	dev->ctrl_dr->wIndex = 0x03;
	dev->ctrl_dr->wLength = writesize;


	usb_fill_control_urb(urb, dev->udev,
			  usb_sndctrlpipe(dev->udev, 0),
			  (unsigned char*)dev->ctrl_dr,
			  buf,
			  writesize,
			  ambx_light_write_ctrl_callback,
			  dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* send the data out the ctrl port */
	retval = usb_submit_urb(urb, GFP_KERNEL);
	mutex_unlock(&dev->io_mutex);
	if (retval) {
		dev_err(&dev->interface->dev,
			"%s - failed submitting write urb, error %d\n",
			__func__, retval);
		goto error_unanchor;
	}

	/*
	 * release our reference to this urb, the USB core will eventually free
	 * it entirely
	 */
	usb_free_urb(urb);


	return writesize;

error_unanchor:
	usb_unanchor_urb(urb);
error:
	if (urb) {
		usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
		usb_free_urb(urb);
	}
	up(&dev->limit_sem);

exit:
	return retval;
}

static const struct file_operations ambx_light_fops = {
	.owner =	THIS_MODULE,
	.read =		NULL,
	.write =	ambx_light_write,
	.open =		ambx_light_open,
	.release =	ambx_light_release,
	.flush =	ambx_light_flush,
	.llseek =	noop_llseek,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver ambx_light_class = {
	.name =		"ambx_light%d",
	.fops =		&ambx_light_fops,
	.minor_base =	CYBORG_AMBX_LIGHT_MINOR_BASE,
};

static int ambx_light_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct usb_ambx_light *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error;
	}
	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&dev->io_mutex);
	spin_lock_init(&dev->err_lock);
	init_usb_anchor(&dev->submitted);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* set up the endpoint information */
	/* use only the first endpoints */
	iface_desc = interface->cur_altsetting;
	if (iface_desc->desc.bNumEndpoints == 1) {
		endpoint = &iface_desc->endpoint[0].desc;
		buffer_size = usb_endpoint_maxp(endpoint);
		dev->ctrl_size = buffer_size;
		dev->ctrl_endpointAddr = endpoint->bEndpointAddress;
		dev->ctrl_buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!dev->ctrl_buffer) {
			dev_err(&interface->dev,
					"Could not allocate ctrl_buffer\n");
			goto error;
		}
		dev->ctrl_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!dev->ctrl_urb) {
			dev_err(&interface->dev,
					"Could not allocate ctrl_urb\n");
			goto error;
		}
	}


	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &ambx_light_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev,
			"Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "Cyborg amBX Light Pods device now attached to amBXLight-%d",
		 interface->minor);
	return 0;

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, ambx_light_delete);
	return retval;
}

static void ambx_light_disconnect(struct usb_interface *interface)
{
	struct usb_ambx_light *dev;
	int minor = interface->minor;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &ambx_light_class);

	/* prevent more I/O from starting */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	usb_kill_anchored_urbs(&dev->submitted);

	/* decrement our usage count */
	kref_put(&dev->kref, ambx_light_delete);

	dev_info(&interface->dev, "Cyborg amBX Light Pods #%d now disconnected", minor);
}

static void ambx_light_draw_down(struct usb_ambx_light *dev)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&dev->submitted);
	usb_kill_urb(dev->ctrl_urb);
}

static int ambx_light_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_ambx_light *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;
	ambx_light_draw_down(dev);
	return 0;
}

static int ambx_light_resume(struct usb_interface *intf)
{
	return 0;
}

static int ambx_light_pre_reset(struct usb_interface *intf)
{
	struct usb_ambx_light *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->io_mutex);
	ambx_light_draw_down(dev);

	return 0;
}

static int ambx_light_post_reset(struct usb_interface *intf)
{
	struct usb_ambx_light *dev = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	dev->errors = -EPIPE;
	mutex_unlock(&dev->io_mutex);

	return 0;
}

static struct usb_driver ambx_light_driver = {
	.name =		"cyborg_ambx_light",
	.probe =	ambx_light_probe,
	.disconnect =	ambx_light_disconnect,
	.suspend =	ambx_light_suspend,
	.resume =	ambx_light_resume,
	.pre_reset =	ambx_light_pre_reset,
	.post_reset =	ambx_light_post_reset,
	.id_table =	ambx_light_table,
	.supports_autosuspend = 1,
};

module_usb_driver(ambx_light_driver);

MODULE_DESCRIPTION("ambxlight");
MODULE_AUTHOR("Yuki Mizuno");
MODULE_LICENSE("GPL v2");
