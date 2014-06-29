/*
 * Cyborg amBX Gaming Light driver
 *
 * Copyright (C) 2014 Yuki Mizuno <u@mzyy94.com>
 *
 */
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/slab.h> 

#include "proc.h"

MODULE_DESCRIPTION("ambxlight");
MODULE_AUTHOR("Yuki Mizuno");
MODULE_LICENSE("GPL v3");


static int __init ambxlight_init(void)
{
	if (attach_proc_entry(THIS_MODULE)) {
		printk(KERN_ERR "ambxlight: [err] %s(%d) create_proc_entry failed\n", __FUNCTION__, __LINE__);
		return -EBUSY;
	}

    printk(KERN_INFO "ambxlight: driver loaded\n");
    return 0;
}


static void __exit ambxlight_exit(void)
{
	detach_proc_entry();
    printk( KERN_INFO "ambxlight: driver removed\n" );
}

module_init(ambxlight_init);
module_exit(ambxlight_exit);
