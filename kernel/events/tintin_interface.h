#ifndef TINTIN_INTERFACE_H
#define TINTIN_INTERFACE_H

#include <linux/tintin_event.h>
#include "tintin_scheduler.h"


struct tintin_read_back {
	u64 count;
	u64 uncertainty;
};

int tintin_read_one(struct perf_event *event, u64 read_format,
			 char __user *buf);

int tintin_read_one_with_uncertainty(struct perf_event *event, u64 read_format,
			 char __user *buf);

int __init init_tintin_interface(void);

ssize_t tintin_event_write(struct file *file, const char __user *buf, size_t count,
			 loff_t *ppos); 

#endif //TINTIN_INTERFACE_H