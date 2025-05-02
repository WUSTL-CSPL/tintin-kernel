
#include <linux/sort.h>
#include <linux/tintin_event.h>
#include "tintin_elastic.h"

// #define TINTIN_SCHED_C_DEUBG

// Exported to /proc/sys
int perf_event_id_to_pin;
EXPORT_SYMBOL(perf_event_id_to_pin);

// Exported to /proc/sys
u64 tintin_sched_interval_ms;
EXPORT_SYMBOL(tintin_sched_interval_ms);

// Import the scheduling quantum
extern u64 tintin_sched_quantum;
/**
  * struct tintin_event
  * Defined in perf_event.h
  */

extern int tintin_sc_index;

//Sort events by weight descending
/*
void generic_swap(void *a, void *b, int size)
{
	char t;

	struct tintin_event **sc1 =
		(struct tintin_event **)a;
	struct tintin_event **sc2 =
		(struct tintin_event **)b;
	void *tmp;

	// struct tintin_event* sc1 = *lhs;
	// printk("[Tintin-Sched] the event at addr addr: 0x%p", *sc1);
	// printk("[Tintin-Swap] uncertainty of sc1: 0x%d", (*sc1)->uncertainty);
	// printk("[Tintin-Swap] uncertainty of sc2: 0x%d", (*sc2)->uncertainty);

	struct tintin_event *sc1_to_swap = *sc1;
	struct tintin_event *sc2_to_swap = *sc2;

	// printk("[Tintin-Swap] addr of sc1: 0x%p", a);
	// printk("[Tintin-Swap] addr of sc2: 0x%p", b);

	// Swap the pointer
	// *tmp = sc1_to_swap;
	*sc1 = *(struct tintin_event **)b;
	*sc2 = sc1_to_swap;

	// do {
	// 	t = *(char *)a;
	// 	*(char *)a++ = *(char *)b;
	// 	*(char *)b++ = t;
	// } while (--size > 0);
}
*/

//Sort events by elasticity descending (more elastic means faster to reach U_min)
int event_remaining_time_desc(const void *lhs, const void *rhs)
{
	struct tintin_event *sc1 =
		*(struct tintin_event **)lhs;
	struct tintin_event *sc2 =
		*(struct tintin_event **)rhs;
	if (sc1->remaining_time > sc2->remaining_time)
		return -1;
	if (sc1->remaining_time < sc2->remaining_time)
		return 1;
	return 0;
}


//Sort events by weight descending
int event_weight_desc(const void *lhs, const void *rhs)
{
	struct tintin_event *sc1 =
		*(struct tintin_event **)lhs;
	struct tintin_event *sc2 =
		*(struct tintin_event **)rhs;
	if (sc1->weight > sc2->weight)
		return -1;
	if (sc1->weight < sc2->weight)
		return 1;
	return 0;
}

// Sort events by weight ascending
int event_weight_asc(const void *lhs, const void *rhs)
{
	struct tintin_event *sc1 =
		*(struct tintin_event **)lhs;
	struct tintin_event *sc2 =
		*(struct tintin_event **)rhs;
	if (sc1->weight < sc2->weight)
		return -1;
	if (sc1->weight > sc2->weight)
		return 1;
	return 0;
}

// Sort events by uncertainty descending
int event_uncertainty_desc(const void *lhs, const void *rhs)
{
	// printk("[Tintin-Uncertainty] the event at addr: 0x%p", lhs);
	struct tintin_event *sc1 =
		*(struct tintin_event **)lhs;
	struct tintin_event *sc2 =
		*(struct tintin_event **)rhs;

	if (sc1->uncertainty > sc2->uncertainty) {
		return -1;
	}
	if (sc1->uncertainty < sc2->uncertainty) {
		return 1;
	}
	return 0;
}

// Sort events by uncertainty ascending
int event_uncertainty_asc(const void *lhs, const void *rhs)
{
	struct tintin_event *sc1 =
		*(struct tintin_event **)lhs;
	struct tintin_event *sc2 =
		*(struct tintin_event **)rhs;
	if (sc1->uncertainty < sc2->uncertainty)
		return -1;
	if (sc1->uncertainty > sc2->uncertainty)
		return 1;
	return 0;
}

// Sort events by time remaining descending, then by uncertainty descending
// This guarantees longest measurement interval,
// then those events that have the most uncertainty are grabbed if they don't have time left
int event_time_uncertainty_desc(const void * lhs, const void * rhs)
{

	struct tintin_event *sc1 =
		*(struct tintin_event **)lhs;
	struct tintin_event *sc2 =
		*(struct tintin_event **)rhs;
	if (sc1->remaining_time > sc2->remaining_time)
		return -1;
	if (sc1->remaining_time < sc2->remaining_time)
		return 1;
	if (sc1->uncertainty > sc2->uncertainty)
		return -1;
	if (sc1->uncertainty < sc2->uncertainty)
		return 1;
	return 0;
}

static int tintin_rr_invocations = 0;
u64 tintin_schedule_rr(struct tintin_event** scs, const int len, const int m, u64 period) {

	int i;
	struct tintin_event *sc;

	// Schedule events in round robin fashion
	for (i = 0; i < len; ++i) {
		sc = scs[i];

		sc->priority = (i + tintin_rr_invocations) % len;
		// printk(KERN_INFO "[Tintin-Scheduler-RR] Event %p, Priority: %llu\n", sc, sc->priority);
	}

	tintin_rr_invocations += m;
#ifdef TINTIN_SCHED_C_DEUBG
	printk(KERN_INFO "[Tintin-Scheduler-RR] Returning timeslice %llu ns\n", timeslice);
#endif
	return period;
}


#define UNCERTAINTY_FIRST_WARMUP 48

int uf_warmup_count = 0;

u64 tintin_schedule_uncertainty_first(struct tintin_event **scs, const int len,
				       const int m, u64 period)
{
	struct tintin_event *sc;
	int i;

	if (uf_warmup_count < UNCERTAINTY_FIRST_WARMUP) {
		// Schedule events in round robin fashion
		for (i = 0; i < len; ++i) {
			sc = scs[i];
			sc->priority = (i + uf_warmup_count) % len;
			// printk(KERN_INFO "[Tintin-Scheduler-RR] Event %p, Priority: %llu\n", sc, sc->priority);
		}
		uf_warmup_count++;
	}

	sort(scs, len, sizeof(struct tintin_event *), &event_uncertainty_desc,
	     NULL);

	// Assign utilizations
	for (i = 0; i < len; ++i) {
		sc = scs[i];
		sc->utilization = period;
		sc->priority = i;
		// sc->priority = tintin_sc_index - i > 0 ? tintin_sc_index - i : 0;

		// sc->priority = sc->uncertainty;

		// printk(KERN_INFO
		//        "[Tintin-Scheduler-UF] Event %p, Priority: %llu, Uncertainty %llu\n",
		//        sc, sc->priority, sc->uncertainty);
	}

	return period;
}

#define MIN_WARMUP_TIMESLICES 20 // Warm up rounds

u64 tintin_schedule(struct tintin_event** scs, const int len, const int m, u64 hyperperiod) {

	int i;
	bool in_warmup = false;
	bool new_hyperperiod = true;
	u64 next_schedule = hyperperiod;
	struct tintin_event *sc;

	// Go through events to see if we're still in warmup phase, or still in hyperperiod
	for (i = 0; i < len; ++i) {
		sc = scs[i];

		//Check if we're past the warmup phase
		if(sc->total_time_monitored < MIN_WARMUP_TIMESLICES * hyperperiod) in_warmup = true;

		//Check if any event has time left in this hyperperiod
		if(sc->remaining_time > 0) new_hyperperiod = false;

		//Less time on counter means higher priority if still in warmup phase
		//Lower priority serviced first
		sc->priority = sc->total_time_monitored;

#ifdef TINTIN_SCHED_C_DEUBG
		printk(KERN_INFO "[Tintin-Scheduler] Event %p, Priority: %llu, Remaining Time: %llu\n", sc, sc->priority, sc->remaining_time);
#endif

	}
	// Still in warmup phase, just use priority set above
	if(in_warmup) {

#ifdef TINTIN_SCHED_C_DEUBG
		printk(KERN_INFO "[Tintin-Scheduler] Returning hyperperiod time %llu ns\n", next_schedule);
#endif
		return next_schedule;
	}

	// Not in the same hyperperiod, use elastic scheduling
	if(new_hyperperiod) {
		tintin_elastic(scs, len, m, hyperperiod);
	}

	//Sort events to assign priorities
	sort(scs, len, sizeof(struct tintin_event*), &event_time_uncertainty_desc, NULL);

	for(i = 0; i < len; ++i) {
		sc = scs[i];
		sc->priority = i;

#ifdef TINTIN_SCHED_C_DEUBG
		printk(KERN_INFO "[Tintin-Scheduler] Event %p, Priority: %llu, Remaining Time: %llu, Uncertainty: %llu\n",
			sc, sc->priority, sc->remaining_time, sc->uncertainty);		
#endif
		//Remaining time should be the shortest of the events to be scheduled next
		if(i < m && sc->remaining_time > 0) next_schedule = sc->remaining_time;
	}
#ifdef TINTIN_SCHED_C_DEUBG
	printk(KERN_INFO "Before [Tintin-Scheduler] Returning timeslice %llu, default timeslice %llu ns\n", next_schedule, timeslice);
#endif
	// next_schedule = (next_schedule < hyperperiod/U_MIN_RATIO) ? hyperperiod/U_MIN_RATIO : next_schedule;
	// We use a separate parameter to represent the minimum timeslice
	next_schedule = (next_schedule < tintin_sched_quantum) ? tintin_sched_quantum : next_schedule;

	next_schedule = (next_schedule > hyperperiod) ? hyperperiod : next_schedule;

#ifdef TINTIN_SCHED_C_DEUBG
	printk(KERN_INFO "After [Tintin-Scheduler] Returning next_schedule %llu, default timeslice %llu ns\n", next_schedule, timeslice);
#endif

	return next_schedule;
}