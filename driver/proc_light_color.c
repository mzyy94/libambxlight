#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#if 0
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/slab.h> 
#endif

#include "proc_light_color.h"

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
