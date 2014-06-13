#ifndef _PROC_LIGHT_COLOR_H__
#define _PROC_LIGHT_COLOR_H__
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>

static ssize_t color_hex_write(struct file *filp, const char *buf, size_t len, loff_t *data);
static ssize_t color_hex_read(struct file *filp, char *buf, size_t len, loff_t *data);
#endif
