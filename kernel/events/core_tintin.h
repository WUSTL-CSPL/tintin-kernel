#ifndef _KERNEL_EVENTS_CORE_TINTIN_H
#define _KERNEL_EVENTS_CORE_TINTIN_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/idr.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/tick.h>
#include <linux/sysfs.h>
#include <linux/dcache.h>
#include <linux/percpu.h>
#include <linux/ptrace.h>
#include <linux/reboot.h>
#include <linux/vmstat.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/hugetlb.h>
#include <linux/rculist.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/anon_inodes.h>
#include <linux/kernel_stat.h>
#include <linux/cgroup.h>
#include <linux/perf_event.h>
#include <linux/trace_events.h>
#include <linux/hw_breakpoint.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/mman.h>
#include <linux/compat.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/namei.h>
#include <linux/parser.h>
#include <linux/sched/clock.h>
#include <linux/sched/mm.h>
#include <linux/proc_ns.h>
#include <linux/mount.h>
#include <linux/min_heap.h>
#include <linux/highmem.h>
#include <linux/pgtable.h>
#include <linux/buildid.h>

#include "internal.h"

extern struct tintin_event *tintin_sc_list[MAX_NUM_TINTIN_SC];
extern int tintin_sc_index;

/*
 * This file export functions in core.c
 */
void perf_event_groups_delete(struct perf_event_groups *groups,
				     struct perf_event *event);


void perf_event_groups_insert(struct perf_event_groups *groups,
				     struct perf_event *event);


// For syscalls
int perf_copy_attr(struct perf_event_attr __user *uattr,
			  struct perf_event_attr *attr);

struct perf_event *perf_event_alloc(
	struct perf_event_attr *attr, int cpu, struct task_struct *task,
	struct perf_event *group_leader, struct perf_event *parent_event,
	perf_overflow_handler_t overflow_handler, void *context, int cgroup_fd);

int perf_event_set_clock(struct perf_event *event, clockid_t clk_id);

struct perf_event_context *find_get_context(struct pmu *pmu,
						   struct task_struct *task,
						   struct perf_event *event);

int perf_event_set_output(struct perf_event *event,
				 struct perf_event *output_event);

bool perf_check_permission(struct perf_event_attr *attr,
				  struct task_struct *task);

void free_event(struct perf_event *event);

struct perf_event_context *
__perf_event_ctx_lock_double(struct perf_event *group_leader,
			     struct perf_event_context *ctx);

void perf_event_ctx_unlock(struct perf_event *event,
				  struct perf_event_context *ctx);

bool exclusive_event_installable(struct perf_event *event,
					struct perf_event_context *ctx);

bool perf_event_validate_size(struct perf_event *event);

int perf_get_aux_event(struct perf_event *event,
			      struct perf_event *group_leader);

void perf_remove_from_context(struct perf_event *event,
				     unsigned long flags);

void perf_install_in_context(struct perf_event_context *ctx,
				    struct perf_event *event, int cpu);

void perf_event__header_size(struct perf_event *event);

void perf_event__id_header_size(struct perf_event *event);



#endif // _KERNEL_EVENTS_CORE_TINTIN_H