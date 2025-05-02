#ifndef _KERNEL_TINTIN_EVENTS_HANDLER_H
#define _KERNEL_TINTIN_EVENTS_HANDLER_H

#include <linux/perf_event.h>
#include <linux/hardirq.h>
#include <linux/sched/clock.h>
#include <linux/sort.h>


#define MAX_INTERRUPTS (~0ULL)

#define MAX_NUM_TINTIN_SC 600 // magic number

#define NUM_HPCS 6

#define DEFAULT_MAX_SCHED_INTERVAL 10000000
#define DEFAULT_SCHED_INTERVAL 4

// Tintin's code

#define SCALE_FOR_FLOATING 1000
#define EVENT_WINDOW_SIZE 50000 // Magic number
#define SCALE_RATE_FOR_VARIANCE 1000000ULL // ns to ms
#define U_MIN_RATIO 10
#define HR_TIMER_MIN 50000ULL // 50us

// Tintin global list

struct tintin_event {

	int context_id;

	bool updated;

	bool pinned;

	int weight;

	u64 priority;

	//int count_variance; // expected variance of unmeasured count
	u64 uncertainty; // sqrt of count_variance
	u64 utilization; // current scheduling quota

	// Duration of most recent monitoring interval
	u64 last_scheduled_in_time;
	u64 last_scheduled_out_time;

	// Total time on counters,
	// but don't want to use Welfords_total_weight in case we're doing sliding window
	u64 total_time_monitored;
	//Total time of monitoring scope, may be greater than monitored if multiplexing
	u64 total_time_enabled; 
	//Total time not monitored (total_time_enabled - total_time_monitored)
	u64 total_time_unmonitored;
	// Remaining time on counters in this hyperperiod
	u64 remaining_time;


	// For Welford's method
	u64 Welfords_total; // Total measured count
	u64 Welfords_total_weight; // Total time on counters
	u64 Welfords_mean; // Mean rate
	u64 Welfords_variance_running; // Numerator of weighted variance
	u64 Welfords_variance; // Weighted variance

	// Tintin event count data vector
	// (Kernel is allowed to use pointer without #include)
	struct tintin_vector * count_vec;
	u64 running_count;

	/**
	  * These two attributes are bound
	  */
	u64* raw_counts;
	u64* raw_time_points;

	u64* count_deltas;
	u64* time_deltas;

	u64* running_times;
	u64* enable_times; // This might not be used
	// u64 total_count; // Already exist // tmp attribute record the total count

	// The assigned events under this scheduling context
	struct perf_event* _perf_event;

	// For linking into the list in the tintin_profiling_scope
	struct list_head list;  
};

typedef struct tintin_event tintin_esx;


// profiling scope list

void init_tintin_scheduling_context(struct tintin_event* sc);

void tintin_dealloc_scheduling_context(struct tintin_event* to_delete_sc);

void tintin_remove_scheduling_context(struct tintin_event* to_delete_sc);

/**
 * Types for scheduling protocol
*/
enum tintin_event_type_t {
	TINTIN_EVENT_FLEXIBLE = 0x1,
	TINTIN_EVENT_PINNED = 0x2,
	TINTIN_EVENT_TIME = 0x4,
	/* see ctx_resched() for details */
	TINTIN_EVENT_CPU = 0x8,
	TINTIN_EVENT_ALL = TINTIN_EVENT_FLEXIBLE | TINTIN_EVENT_PINNED,
};

/**
 * Tintin's event scheduling context - event context structure
 *
 * Used as a container for task events and CPU events as well:
 */

/**
 * Functions
*/

void tintin_update_uncertainty_from_rate_of_change(struct tintin_event* sc);

u64 tintin_mux_hrtimer_handler(struct hrtimer *hr);

struct tintin_event *tintin_esx_alloc(void);

int tintin_esx_init(struct tintin_event *tintin_e, struct perf_event *event);

int tintin_esx_add(struct tintin_event *tintin_e);

void tintin_print_event(struct perf_event *event, bool is_CPU_context);

void tintin_print_tintin_esx(struct tintin_event *esc);

void tintin_print_scheduling_context(struct tintin_event* sc);

#endif /* _KERNEL_TINTIN_EVENTS_HANDLER_H */