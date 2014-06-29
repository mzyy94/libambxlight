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
	struct urb		*bulk_in_urb;		/* the urb to read data with */
	unsigned char           *bulk_in_buffer;	/* the buffer to receive data */
	size_t			bulk_in_size;		/* the size of the receive buffer */
	size_t			bulk_in_filled;		/* number of bytes in the buffer */
	size_t			bulk_in_copied;		/* already copied to user space */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */

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
	wait_queue_head_t	bulk_in_wait;		/* to wait for an ongoing read */
};
#define to_ambx_light_dev(d) container_of(d, struct usb_ambx_light, kref)

static struct usb_driver ambx_light_driver;
static void ambx_light_draw_down(struct usb_ambx_light *dev);

static void ambx_light_delete(struct kref *kref)
{
	struct usb_ambx_light *dev = to_ambx_light_dev(kref);

	usb_free_urb(dev->bulk_in_urb);
	usb_free_urb(dev->ctrl_urb); /* new */
	usb_put_dev(dev->udev);
	kfree(dev->bulk_in_buffer);
	kfree(dev->ctrl_buffer); /* new */
	kfree(dev->ctrl_dr); /* new */
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

#if 1
static void ambx_light_read_bulk_callback(struct urb *urb)
{
	struct usb_ambx_light *dev;

	dev = urb->context;

	spin_lock(&dev->err_lock);
	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			dev_err(&dev->interface->dev,
				"%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);

		dev->errors = urb->status;
	} else {
		dev->bulk_in_filled = urb->actual_length;
	}
	dev->ongoing_read = 0;
	spin_unlock(&dev->err_lock);

	wake_up_interruptible(&dev->bulk_in_wait);
}
#else

/* new */
static void ambx_light_ctrl_callback(struct urb *urb)
{
	struct usb_ambx_light *dev;

	dev = urb->context;
}
#endif

static int ambx_light_do_read_io(struct usb_ambx_light *dev, size_t count)
{
	int rv;

	/* prepare a read */
	usb_fill_bulk_urb(dev->bulk_in_urb,
			dev->udev,
			usb_rcvbulkpipe(dev->udev,
				dev->bulk_in_endpointAddr),
			dev->bulk_in_buffer,
			min(dev->bulk_in_size, count),
			ambx_light_read_bulk_callback,
			dev);
	/* tell everybody to leave the URB alone */
	spin_lock_irq(&dev->err_lock);
	dev->ongoing_read = 1;
	spin_unlock_irq(&dev->err_lock);

	/* submit bulk in urb, which means no data to deliver */
	dev->bulk_in_filled = 0;
	dev->bulk_in_copied = 0;

	/* do it */
	rv = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);
	if (rv < 0) {
		dev_err(&dev->interface->dev,
			"%s - failed submitting read urb, error %d\n",
			__func__, rv);
		rv = (rv == -ENOMEM) ? rv : -EIO;
		spin_lock_irq(&dev->err_lock);
		dev->ongoing_read = 0;
		spin_unlock_irq(&dev->err_lock);
	}

	return rv;
}

static ssize_t ambx_light_read(struct file *file, char *buffer, size_t count,
			 loff_t *ppos)
{
	struct usb_ambx_light *dev;
	int rv;
	bool ongoing_io;

	dev = file->private_data;

	/* if we cannot read at all, return EOF */
	if (!dev->bulk_in_urb || !count)
		return 0;

	/* no concurrent readers */
	rv = mutex_lock_interruptible(&dev->io_mutex);
	if (rv < 0)
		return rv;

	if (!dev->interface) {		/* disconnect() was called */
		rv = -ENODEV;
		goto exit;
	}

	/* if IO is under way, we must not touch things */
retry:
	spin_lock_irq(&dev->err_lock);
	ongoing_io = dev->ongoing_read;
	spin_unlock_irq(&dev->err_lock);

	if (ongoing_io) {
		/* nonblocking IO shall not wait */
		if (file->f_flags & O_NONBLOCK) {
			rv = -EAGAIN;
			goto exit;
		}
		/*
		 * IO may take forever
		 * hence wait in an interruptible state
		 */
		rv = wait_event_interruptible(dev->bulk_in_wait, (!dev->ongoing_read));
		if (rv < 0)
			goto exit;
	}

	/* errors must be reported */
	rv = dev->errors;
	if (rv < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		rv = (rv == -EPIPE) ? rv : -EIO;
		/* report it */
		goto exit;
	}

	/*
	 * if the buffer is filled we may satisfy the read
	 * else we need to start IO
	 */

	if (dev->bulk_in_filled) {
		/* we had read data */
		size_t available = dev->bulk_in_filled - dev->bulk_in_copied;
		size_t chunk = min(available, count);

		if (!available) {
			/*
			 * all data has been used
			 * actual IO needs to be done
			 */
			rv = ambx_light_do_read_io(dev, count);
			if (rv < 0)
				goto exit;
			else
				goto retry;
		}
		/*
		 * data is available
		 * chunk tells us how much shall be copied
		 */

		if (copy_to_user(buffer,
				 dev->bulk_in_buffer + dev->bulk_in_copied,
				 chunk))
			rv = -EFAULT;
		else
			rv = chunk;

		dev->bulk_in_copied += chunk;

		/*
		 * if we are asked for more than we have,
		 * we start IO but don't wait
		 */
		if (available < count)
			ambx_light_do_read_io(dev, count - chunk);
	} else {
		/* no data in the buffer */
		rv = ambx_light_do_read_io(dev, count);
		if (rv < 0)
			goto exit;
		else
			goto retry;
	}
exit:
	mutex_unlock(&dev->io_mutex);
	return rv;
}

#if 0
static void ambx_light_write_bulk_callback(struct urb *urb)
{
	struct usb_ambx_light *dev;

	dev = urb->context;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			dev_err(&dev->interface->dev,
				"%s - nonzero write bulk status received: %d\n",
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
#endif

/* new */
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

	/* this lock makes sure we don't submit URBs to gone devices */
	mutex_lock(&dev->io_mutex);
	if (!dev->interface) {		/* disconnect() was called */
		mutex_unlock(&dev->io_mutex);
		retval = -ENODEV;
		goto error;
	}

#if 0
	/* initialize the urb properly */
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
			  buf, writesize, ambx_light_write_bulk_callback, dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(urb, &dev->submitted);
#else

#if 0
	printk(KERN_INFO "buffsize = %d\n", writesize);
	unsigned char setuppacket[8] = {0x21, 0x09, 0xa2, 0x03, 0x00, 0x00, 0x09, 0x00};
	struct usb_ctrlrequest setup_packet = {
		.bRequestType = 0x21,
		.bRequest = 0x09,
		.wValue = 0xa3,
		.wIndex = 0x03,
		.wLength = 0x09,
	};
#endif

	/* initialize the urb properly */
	dev->ctrl_dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!dev->ctrl_dr) {
		retval = -ENOMEM;
		goto error;
	}
	dev->ctrl_dr->bRequestType = 0x21;
	dev->ctrl_dr->bRequest = 0x09;
	dev->ctrl_dr->wValue = 0xa2;
	dev->ctrl_dr->wIndex = 0x03;
	dev->ctrl_dr->wLength = 0x09;


	usb_fill_control_urb(urb, dev->udev,
			  //usb_sndctrlpipe(dev->udev, dev->ctrl_endpointAddr),
			  usb_sndctrlpipe(dev->udev, 0),
			  (unsigned char*)dev->ctrl_dr,
			  buf,
			  writesize,
			  ambx_light_write_ctrl_callback,
			  dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	//usb_anchor_urb(urb, &dev->submitted);
#endif

	/* send the data out the bulk port */
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
	.read =		ambx_light_read,
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
	int i;
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
	init_waitqueue_head(&dev->bulk_in_wait);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    usb_endpoint_is_bulk_in(endpoint)) {
			/* we found a bulk in endpoint */
			buffer_size = usb_endpoint_maxp(endpoint);
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			if (!dev->bulk_in_buffer) {
				dev_err(&interface->dev,
					"Could not allocate bulk_in_buffer\n");
				goto error;
			}
			dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!dev->bulk_in_urb) {
				dev_err(&interface->dev,
					"Could not allocate bulk_in_urb\n");
				goto error;
			}
		}

		if (!dev->bulk_out_endpointAddr &&
		    usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}
#if 0
	if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
		dev_err(&interface->dev,
			"Could not find both bulk-in and bulk-out endpoints\n");
		goto error;
	}
#else
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

#endif

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
	usb_kill_urb(dev->bulk_in_urb);
	/* new */
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
