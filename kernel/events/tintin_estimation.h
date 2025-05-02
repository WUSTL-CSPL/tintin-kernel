#ifndef TINTIN_ESTIMATION_H
#define TINTIN_ESTIMATION_H

#include <linux/kernel.h>
#include <linux/tintin_event.h>

u64 tintin_interpolate_count_by_TAM(struct tintin_event *tintin_event);

u64 tintin_TAM_first_measurement(u64 count, u64 time_enabled, u64 time_monitored);

u64 tintin_TAM(u64 curr_count, u64 curr_time_enabled, u64 curr_hpc_time,
	       u64 prev_count, u64 prev_time_enabled, u64 prev_hpc_time);

u64 tintin_running_count_extrapolated(struct tintin_event *tintin_event);

u64 tintin_uncertainty_extrapolated(struct tintin_event *tintin_event);

#endif //TINTIN_ESTIMATION_H