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

#include "proc_light_color.c"

#define PROC_ROOT_DIR "ambx"
#define PROC_LIGHT_DIR "light"
#define PROC_COLOR_DIR "colord"
#define PROC_COLOR_ENTRY_HEX "hex"


MODULE_DESCRIPTION("ambxlight");
MODULE_AUTHOR("Yuki Mizuno");
MODULE_LICENSE("GPL v3");


struct proc_dir_entry* rootd;
struct proc_dir_entry* lightd;
struct proc_dir_entry* colord;
struct proc_dir_entry* proc_color_entry_hex;

static int __init ambxlight_init(void)
{
	static const struct file_operations fops = {
		.owner = THIS_MODULE,
		.read = color_hex_read,
		.write = color_hex_write
	};
	rootd = proc_mkdir(PROC_ROOT_DIR, NULL);
	lightd = proc_mkdir(PROC_LIGHT_DIR, rootd);
	colord = proc_mkdir(PROC_COLOR_DIR, lightd);
	proc_color_entry_hex = proc_create(PROC_COLOR_ENTRY_HEX, 0, colord, &fops);
	if ( proc_color_entry_hex == NULL) {
		printk(KERN_ERR "create_proc_entry failed\n");
		return -EBUSY;
	}

    printk(KERN_INFO "ambxlight is loaded\n");
    return 0;
}


static void __exit ambxlight_exit(void)
{
	remove_proc_entry(PROC_COLOR_ENTRY_HEX, colord);
	remove_proc_subtree(PROC_COLOR_DIR, lightd);
	remove_proc_subtree(PROC_LIGHT_DIR, rootd);
	remove_proc_subtree(PROC_ROOT_DIR, NULL);

    printk( KERN_INFO "ambxlight is removed\n" );
}

module_init(ambxlight_init);
module_exit(ambxlight_exit);
