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

struct proc_dir_entry* root_dir;
struct proc_dir_entry* light_dir;
struct proc_dir_entry* color_dir;
struct proc_dir_entry* proc_color_entry_hex;


#define PROC_ROOT_DIR "ambx"
#define PROC_LIGHT_DIR "light"
#define PROC_COLOR_DIR "colord"
#define PROC_COLOR_ENTRY_HEX "hex"

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

int attach_proc_entry(struct module *this_module) {
	static const struct file_operations fops = {
		.owner = THIS_MODULE,
		.read = color_hex_read,
		.write = color_hex_write
	};
	root_dir = proc_mkdir(PROC_ROOT_DIR, NULL);
	light_dir = proc_mkdir(PROC_LIGHT_DIR, root_dir);
	color_dir = proc_mkdir(PROC_COLOR_DIR, light_dir);
	return (proc_color_entry_hex = proc_create(PROC_COLOR_ENTRY_HEX, 0, color_dir, &fops)) == NULL ? -1 : 0;
}

int detach_proc_entry() {
	remove_proc_entry(PROC_COLOR_ENTRY_HEX, color_dir);
	remove_proc_subtree(PROC_COLOR_DIR, light_dir);
	remove_proc_subtree(PROC_LIGHT_DIR, root_dir);
	remove_proc_subtree(PROC_ROOT_DIR, NULL);
	return 0;
}
