#ifndef _PROC_LIGHT_COLOR_H__
#define _PROC_LIGHT_COLOR_H__
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>

int attach_proc_entry(struct module *this_module);
int detach_proc_entry(void);
#endif
