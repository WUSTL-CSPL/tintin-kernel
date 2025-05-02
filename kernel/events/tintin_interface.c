#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/perf_event.h>
#include <linux/sysctl.h>

#include "internal.h"
#include "tintin_estimation.h"
#include "tintin_interface.h"
#include <linux/tintin_event.h>
#include "tintin_scheduler.h"

/*
 * Begin - Count Reading
 */

static inline u64 tintin_event_count(struct perf_event *event)
{
	struct tintin_event *sc;
	struct tintin_vector *tv;
	struct tintin_measurement *last_measurement;
	u64 total, current_measurement_count, time_on_hpc;
	sc = (struct tintin_event *)event->tintin_esx;
	tv = sc->count_vec;

	/*
	 * If the event is currently on the PMU, we want to read directly
	 * off the PMU and interpolate with our last measurement, instead of
	 * reading our last measurement, which would not include the counts
	 * currently being recorded.
	 */

	//Trigger a read to update event delta
	(void)perf_event_read(event, false);
	if (event->state == PERF_EVENT_STATE_ACTIVE &&
	    event->total_time_running > sc->total_time_monitored) {
		time_on_hpc =
			event->total_time_running - sc->total_time_monitored;
		current_measurement_count = event->delta;
		if (tv->size == 0) {
			// perf_current_measurement_count = perf_event_read(event, false);
			// printk(KERN_INFO "Tintin event=%lld read when active first case. Current measurement=%llu time_from_last_measurement=%llu time_on_hpc=%llu\n",
			//    event->id, current_measurement_count, event->total_time_enabled, time_on_hpc);
			total = tintin_TAM_first_measurement(
				current_measurement_count,
				event->total_time_enabled, time_on_hpc);
		} else {
			last_measurement =
				tintin_vector_get_measurement_by_index(tv, 0);
			//perf_current_measurement_count = perf_event_read(event, false) - last_measurement->cumulative_count;
			// printk(KERN_INFO "Tintin event=%lld read when active. prev_count=%llu prev_time_enabled=%llu prev_time_on_hpc=%llu curr_count=%llu curr_time_from_last_measurement=%llu curr_on_hpc_time=%llu\n",
			//     event->id, last_measurement->count, last_measurement->total_time_enabled, last_measurement->on_HPC_time,
			// 		   current_measurement_count, event->total_time_enabled - last_measurement->total_time_enabled, time_on_hpc);
			total = tintin_TAM(current_measurement_count,
					   event->total_time_enabled,
					   time_on_hpc, last_measurement->count,
					   last_measurement->total_time_enabled,
					   last_measurement->on_HPC_time);
		}

		// printk(KERN_INFO "Tintin event=%lld read when active interpolation=%llu\n",
		//        event->id, total);

		return sc->running_count + total;
	}

	// Can be invoked any time,
	// so perf_event_update_time has not necessarily already been called
	return tintin_running_count_extrapolated(sc);
}

static u64 __tintin_event_read_value(struct perf_event *event)
{
	struct perf_event *child;
	u64 total;
	total = 0;

	mutex_lock(&event->child_mutex);

	total += tintin_event_count(event);
	list_for_each_entry (child, &event->child_list, child_list) {
		total += tintin_event_count(child);
	}

	mutex_unlock(&event->child_mutex);

	return total;
}

int tintin_read_one(struct perf_event *event, u64 read_format, char __user *buf)
{
	u64 enabled, running;
	u64 values[4];
	int n = 0;

	// TODO msudvarg:
	// Updating time may be unnecessary now because tintin_event_count calls perf_event_read
	// Later we can try to remove to reduce overhead
	// But for now, leave it, to avoid possible bugs
	perf_event_update_time(event);
	values[n++] = __tintin_event_read_value(event);

	// printk(KERN_INFO "[Tintin-Read] Total count value after extraplation for event %lld: %llu\n", event->id, total);

	////////////////////////////////
	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		values[n++] = 0;
	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		values[n++] = 0;
	if (read_format & PERF_FORMAT_ID)
		values[n++] = 0;

	if (copy_to_user(buf, values, n * sizeof(u64)))
		return -EFAULT;

	return n * sizeof(u64);
}

int tintin_read_one_with_uncertainty(struct perf_event *event, u64 read_format,
				     char __user *buf)
{
	struct tintin_event *sc;
	struct tintin_read_back read_back;

	// TODO msudvarg:
	// Updating time may be unnecessary now because tintin_event_count calls perf_event_read
	// Later we can try to remove to reduce overhead
	// But for now, leave it, to avoid possible bugs
	perf_event_update_time(event);

	sc = (struct tintin_event *)event->tintin_esx;
	read_back.count = __tintin_event_read_value(event);
	read_back.uncertainty = tintin_uncertainty_extrapolated(sc);

	// printk(KERN_INFO "[Tintin-Read] Total count value after extraplation for event %lld: %llu, uncertainty %llu\n", event->id, read_back.count, read_back.uncertainty);

	if (copy_to_user(buf, &read_back, sizeof(struct tintin_read_back))) {
		return -EFAULT;
	}

	return sizeof(struct tintin_read_back);
}

/*
 * Configure the event at runtime
 */

ssize_t tintin_event_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
#define TINTIN_BUFFER_SIZE 16 // 16 is a magic number
	char kernel_buffer[TINTIN_BUFFER_SIZE];

	struct perf_event *event = file->private_data;
	struct tintin_event *sc = event->tintin_esx;

	// Check if the count exceeds the buffer size
	if (count > sizeof(kernel_buffer)) {
		return -EINVAL; // Return an error if the input is too large
	}

	// Copy data from user space to kernel space
	if (copy_from_user(kernel_buffer, buf, count)) {
		return -EFAULT; // Return an error if the copy failed
	}

// Use a data strucutre to handle this
#ifdef TINTIN_INTERFACE_DEBUG
	printk(KERN_INFO "Data written: %s\n", kernel_buffer);
#endif

	if (strncmp(kernel_buffer, "pin", strlen("pin")) == 0) {
		sc->pinned = true;
#ifdef TINTIN_INTERFACE_DEBUG
		printk(KERN_INFO "Pinning the event\n");
#endif
	} else if (strncmp(kernel_buffer, "unpin", strlen("unpin")) == 0) {
		sc->pinned = false;
#ifdef TINTIN_INTERFACE_DEBUG
		printk(KERN_INFO "Stop pinning the event\n");
#endif
	}

	// TODO set the event pinned or unpinned

	return 0; // Return the number of bytes written
}

// End - Counter Reading

/*
 * Begin - Perf Event to Pin
 */

extern int perf_event_id_to_pin; // We pin an even on a HPC as the ground truth

static struct ctl_table event_to_pin_ctl_table[] = {
	{
		.procname = "event_id_to_pin",
		.data = &perf_event_id_to_pin,
		.maxlen = sizeof(perf_event_id_to_pin),
		.mode = 0666,
		.proc_handler = &proc_dointvec,
	},
	{}
};

// End - Perf Event to Pin

/*
 * Begin - Tintin Max Sched Interval
 */

extern u64
	tintin_sched_interval_ms; // We pin an even on a HPC as the ground truth

static struct ctl_table tintin_sched_interval_ctl_table[] = {
	{
		.procname = "tintin_sched_interval",
		.data = &tintin_sched_interval_ms,
		.maxlen = sizeof(tintin_sched_interval_ms),
		.mode = 0666,
		.proc_handler = &proc_dointvec,
	},
	{}
};

// End -Tintin Max Sched Interval

/*
 * Begin - Adjust Profiling Frequency
 */

extern int tintin_sched_quantum;

static struct ctl_table scheduling_quantum_ctl_table[] = {
	{
		.procname = "tintin_sched_quantum",
		.data = &tintin_sched_quantum,
		.maxlen = sizeof(tintin_sched_quantum),
		.mode = 0666,
		.proc_handler = &proc_dointvec,
	},
	{}
};

// End - Adjust Profiling Frequency

/*
 * Begin - Turn on/off Tintin Scheduling
 */

extern int tintin_switch_on;

static struct ctl_table tintin_switch_on_ctl_table[] = {
	{
		.procname = "tintin_sched_switch_on",
		.data = &tintin_switch_on,
		.maxlen = sizeof(tintin_switch_on),
		.mode = 0666,
		.proc_handler = &proc_dointvec,
	},
	{}
};

// End - Turn on/off Tintin Scheduling

/*
 * Begin - Adjust Scheduling Algorithm
 */

extern int tintin_scheduling_policy;

static struct ctl_table sched_policy_ctl_table[] = {
	{
		.procname = "tintin_sched_policy",
		.data = &tintin_scheduling_policy,
		.maxlen = sizeof(tintin_scheduling_policy),
		.mode = 0666,
		.proc_handler = &proc_dointvec,
	},
	{}
};

// End - Adjust Scheduling Algorithm

/*
 * Begin - Turn on CounterMiner
 */

extern int defined_counterminer;

static struct ctl_table counterminer_ctl_table[] = {
	{
		.procname = "counterminer_on",
		.data = &defined_counterminer,
		.maxlen = sizeof(defined_counterminer),
		.mode = 0666,
		.proc_handler = &proc_dointvec,
	},
	{}
};

// End - Turn on CounterMiner

/*
 * Begin - Turn on BayesPerf
 */

extern int defined_bayesperf;

static struct ctl_table bayesperf_ctl_table[] = {
	{
		.procname = "bayesperf_on",
		.data = &defined_bayesperf,
		.maxlen = sizeof(defined_bayesperf),
		.mode = 0666,
		.proc_handler = &proc_dointvec,
	},
	{}
};

// End - Turn on BayesPerf

/*
 * This file will be used as the root directory of Tintin in /proc/sys
 */

static struct ctl_table tintin_ctl_tlb[] = {
	{
		.procname = "tintin",
		.mode = 0555,
		.child = scheduling_quantum_ctl_table,
	},
	{
		.procname = "tintin",
		.mode = 0555,
		.child = event_to_pin_ctl_table,
	},
	{
		.procname = "tintin",
		.mode = 0555,
		.child = tintin_sched_interval_ctl_table,
	},
	{
		.procname = "tintin",
		.mode = 0555,
		.child = tintin_switch_on_ctl_table,
	},
	{
		.procname = "tintin",
		.mode = 0555,
		.child = sched_policy_ctl_table,
	},
	{
		.procname = "tintin",
		.mode = 0555,
		.child = counterminer_ctl_table,
	},
	{
		.procname = "tintin",
		.mode = 0555,
		.child = bayesperf_ctl_table,
	},
	{}
};

int __init init_tintin_interface(void)
{
	register_sysctl_table(tintin_ctl_tlb);
	return 0;
}
