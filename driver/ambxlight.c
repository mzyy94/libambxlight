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


#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>

#include "../include/ambxlight_params.h"
#include "../include/ambxlight_ioctl.h"

/* Define these values to match your devices */
#define CYBORG_AMBX_LIGHT_VENDOR_ID	0x06a3
#define CYBORG_AMBX_LIGHT_PRODUCT_ID	0x0dc5

#define PROC_ROOT_DIR "ambx"
#define PROC_LIGHT_DIR "light"
#define PROC_COLOR_DIR "color"
#define PROC_SPEED_DIR "speed"
#define PROC_COLOR_ENTRY_HEX "hex"

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
	union ambxlight_params params;		/* ambx device parameters */
	unsigned char	transfer_mode;		/* transfer mode configured by ioctl */
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

static void ambx_light_read_ctrl_callback(struct urb *urb)
{
	struct usb_ambx_light *dev;

	dev = urb->context;

	if (urb->actual_length == 9) {
		unsigned char i;
		dev_info(&dev->interface->dev,
		"load parameters: %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			 ((char *)urb->transfer_buffer)[0] & 0xff, /* 0xb0 */
			 ((char *)urb->transfer_buffer)[1] & 0xff,
			 ((char *)urb->transfer_buffer)[2] & 0xff,
			 ((char *)urb->transfer_buffer)[3] & 0xff,
			 ((char *)urb->transfer_buffer)[4] & 0xff,
			 ((char *)urb->transfer_buffer)[5] & 0xff,
			 ((char *)urb->transfer_buffer)[6] & 0xff,
			 ((char *)urb->transfer_buffer)[7] & 0xff,
			 ((char *)urb->transfer_buffer)[8] & 0xff
			 );
		for (i = 0; i < urb->actual_length; i++) {
			dev->params.raw[i] = ((char *)urb->transfer_buffer)[i] & 0xff;
		}
	}

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

static ssize_t ambx_light_read(struct file *file, char *user_buffer,
			  size_t count, loff_t *ppos)
{
	struct usb_ambx_light *dev;
	static unsigned outbyte = 9;
	unsigned char *str;

	dev = file->private_data;
	str = dev->params.raw;

	if( count > outbyte ) {
		count = outbyte;
	}
	outbyte = outbyte - count;
	if (copy_to_user(user_buffer, str, count)) return -EFAULT;
	if (count == 0) {
		outbyte = 9;
	}
	return count;

}

static ssize_t ambx_light_get_params(struct usb_ambx_light *dev)
{
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;

	/*
	 * limit the number of URBs in flight to stop a user from using up all
	 * RAM
	 */
	if (down_interruptible(&dev->limit_sem)) {
		retval = -ERESTARTSYS;
		goto exit;
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

	buf = usb_alloc_coherent(dev->udev, 11, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!buf) {
		retval = -ENOMEM;
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
	dev->ctrl_dr->bRequestType = 0xa1;
	dev->ctrl_dr->bRequest = 0x01;
	dev->ctrl_dr->wValue = 0x0b;
	dev->ctrl_dr->wIndex = 0x03;
	dev->ctrl_dr->wLength = 0x0b;

	usb_fill_control_urb(urb, dev->udev,
			  usb_rcvctrlpipe(dev->udev, 0),
			  (unsigned char*)dev->ctrl_dr,
			  buf,
			  11,
			  ambx_light_read_ctrl_callback,
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

	return 0;

error_unanchor:
	usb_unanchor_urb(urb);
error:
	if (urb) {
		usb_free_coherent(dev->udev, 11, buf, urb->transfer_dma);
		usb_free_urb(urb);
	}
	up(&dev->limit_sem);

exit:
	return retval;
}

struct proc_dir_entry* root_dir;
struct proc_dir_entry* light_dir;
struct proc_dir_entry* color_dir;
struct proc_dir_entry* proc_color_entry_hex;

#define MAXBUF 64
static char modtest_buf[ MAXBUF ];
static int buflen;
static unsigned long outbyte = 0;

static ssize_t color_hex_write(struct file *filp, const char *buf, size_t len, loff_t *data)
{
	if (len >= MAXBUF) {
		printk( KERN_WARNING "input length must be < %d, len = %lu\n", MAXBUF, len);
		return -ENOSPC;
	}

	if (copy_from_user(modtest_buf, buf, len)) return -EFAULT;
	modtest_buf[len] = '\0';
	buflen = len;
	outbyte = buflen;

	return len;
}

static ssize_t color_hex_read(struct file *filp, char *buf, size_t len, loff_t *data)
{
	if (len > outbyte) {
		len = outbyte;
	} 
	outbyte = outbyte - len; 
	if (copy_to_user(buf, modtest_buf, len)) return -EFAULT;
	if (len == 0) {
		outbyte = buflen;
	}
	return len;
}

static int __init ambxlight_init(void)
{
	static const struct file_operations fops = {
		.owner = THIS_MODULE,
		.read = color_hex_read,
		.write = color_hex_write
	};
	root_dir = proc_mkdir(PROC_ROOT_DIR, NULL);
	light_dir = proc_mkdir(PROC_LIGHT_DIR, root_dir);
	color_dir = proc_mkdir(PROC_COLOR_DIR, light_dir);
	proc_color_entry_hex = proc_create(PROC_COLOR_ENTRY_HEX, 0, color_dir, &fops);
	if (proc_color_entry_hex == NULL) {
		printk(KERN_ERR "ambxlight: [err] %s(%u): create_proc_entry failed\n", __FUNCTION__, __LINE__);
		return -EBUSY;
	}

    printk(KERN_INFO "ambxlight: driver loaded\n");
    return 0;
}

static ssize_t ambx_light_pre_get_params(struct usb_ambx_light *dev);

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

	if (urb->actual_length == 2) {
		ambx_light_get_params(dev);
	} else if (urb->actual_length > 2 && urb->actual_length < 5) {
		ambx_light_pre_get_params(dev);
	}

	remove_proc_entry(PROC_COLOR_ENTRY_HEX, color_dir);
	remove_proc_subtree(PROC_COLOR_DIR, light_dir);
	remove_proc_subtree(PROC_LIGHT_DIR, root_dir);
	remove_proc_subtree(PROC_ROOT_DIR, NULL);

    printk( KERN_INFO "ambxlight: driver removed\n" );
}

static ssize_t ambx_light_write(struct file *file, const char *user_buffer,
			  size_t count, loff_t *ppos)
{
	struct usb_ambx_light *dev;
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	char *userdata = NULL;
	size_t writesize = min(count, (size_t)MAX_TRANSFER);
	int retlen = writesize;

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

	userdata = usb_alloc_coherent(dev->udev, writesize, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!userdata) {
		retval = -ENOMEM;
		goto error;
	}

	if (copy_from_user(userdata, user_buffer, writesize)) {
		retval = -EFAULT;
		goto error;
	}

	switch (dev->transfer_mode) {
		unsigned char i;
		default:
			dev->transfer_mode = AMBXLIGHT_MODE_HEXSTRING;
		case AMBXLIGHT_MODE_HEXSTRING:
			if (writesize != 6 && writesize != 7) { /* hex string + line break */
				retval = -EFAULT;
				goto error;
			}
			for (i = 0; i < 6; i++) {
				if (userdata[i] >= '0' && userdata[i] <= '9') {
					userdata[i] -= '0';
				} else if (userdata[i] >= 'a' && userdata[i] <= 'f') {
					userdata[i] -= 'a' - 10;
				} else if (userdata[i] >= 'A' && userdata[i] <= 'F') {
					userdata[i] -= 'A' - 10;
				} else {
					retval = -EFAULT;
					goto error;
				}
				userdata[i/2] = i % 2 ? userdata[i/2] | userdata[i] : userdata[i] << 4;
			}
			writesize = 3;
		case AMBXLIGHT_MODE_COLOR:
			/* check data format */
			if (writesize != 3) {
				retval = -EFAULT;
				goto error;
			}
			writesize = 9;
			buf = usb_alloc_coherent(dev->udev, writesize, GFP_KERNEL,
					&urb->transfer_dma);
			if (!buf) {
				retval = -ENOMEM;
				goto error;
			}
			((char *)buf)[0] = 0xa2;
			((char *)buf)[1] = 0x00;
			((char *)buf)[2] = userdata[0] & 0xff;
			((char *)buf)[3] = userdata[1] & 0xff;
			((char *)buf)[4] = userdata[2] & 0xff;
			((char *)buf)[5] = 0x00;
			((char *)buf)[6] = 0x00;
			((char *)buf)[7] = 0x00;
			((char *)buf)[8] = 0x00;

			break;
		case AMBXLIGHT_MODE_RAW:
			/*
			 * urb data packet format
			 *
			 * |  00  |  01  |  02  | 03.. |
			 * |OPCODE| 0x00 |  values...  |
			 *
			 */
			buf = userdata;

			/* check data format */
			if (writesize < 2 || (buf[1] && 0xff) != 0x00) {
				retval = -EFAULT;
				goto error;
			}

			/* check data size */
			switch (buf[0] & 0xff) {
				case 0xa1: /* set device state */
				case 0xa5: /* set height */
				case 0xa6: /* set intensity */
					if (writesize != 3) {
						retval = -EFAULT;
						goto error;
					}
					break;
				case 0xa2: /* chenge light color */
					if (writesize != 9) {
						retval = -EFAULT;
						goto error;
					}
					break;
				case 0xa3:
					/* unknown */
					break;
				case 0xa4: /* set location */
					if (writesize != 4) {
						retval = -EFAULT;
						goto error;
					}
					break;
				case 0xa7: /* prepare read parameters */
					if (writesize != 2) {
						retval = -EFAULT;
						goto error;
					}
					break;
				default:
					retval = -EFAULT;
					goto error;
			}
			break;
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


	return retlen;

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

static ssize_t ambx_light_pre_get_params(struct usb_ambx_light *dev)
{
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	size_t writesize = 2;

	/*
	 * limit the number of URBs in flight to stop a user from using up all
	 * RAM
	 */
	if (down_interruptible(&dev->limit_sem)) {
		retval = -ERESTARTSYS;
		goto exit;
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
	((char *)buf)[0] = 0xa7;
	((char *)buf)[1] = 0x00;

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

static long ambx_light_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct usb_ambx_light *dev;
	int retval = 0;
	unsigned char mode;

	dev = file->private_data;

	spin_lock_irq(&dev->err_lock);
	switch (cmd) {
		case AMBXLIGHT_IOCTL_SET:
			if (copy_from_user(&mode, (const char *)arg, sizeof(mode))) {
				retval = -EFAULT;
				break;
			}
			dev->transfer_mode = mode;
			break;
		case AMBXLIGHT_IOCTL_GET:
			retval = copy_to_user((char *)arg, &dev->transfer_mode, sizeof(dev->transfer_mode));
			break;
		default:
			dev->transfer_mode = AMBXLIGHT_MODE_RAW;
			retval =  -ENOIOCTLCMD;
	}
	spin_unlock_irq(&dev->err_lock);
	return retval;
}

static const struct file_operations ambx_light_fops = {
	.owner =	THIS_MODULE,
	.read =		ambx_light_read,
	.write =	ambx_light_write,
	.open =		ambx_light_open,
	.unlocked_ioctl =	ambx_light_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	ambx_light_ioctl,
#endif
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

	ambx_light_pre_get_params(dev);
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
