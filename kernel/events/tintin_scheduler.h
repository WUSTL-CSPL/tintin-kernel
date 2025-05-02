#ifndef TINTIN_SCHEDULER_H
#define TINTIN_SCHEDULER_H

#include <linux/tintin_event.h>

// Function to swap two memory regions
// Parameters:
//   a, b: Pointers to memory regions to be swapped
//   size: Size of each memory region
void generic_swap(void *a, void *b, int size);

// Comparator function for sorting events by weight in descending order
// Parameters:
//   lhs, rhs: Pointers to tintin_scheduling_context structures to compare
// Returns:
//   1 if lhs should come before rhs
//   -1 if rhs should come before lhs
//   0 if they are considered equal
int event_weight_desc(const void *lhs, const void *rhs);

// Comparator function for sorting events by weight in ascending order
int event_weight_asc(const void *lhs, const void *rhs);

// Comparator function for sorting events by uncertainty in descending order
int event_uncertainty_desc(const void *lhs, const void *rhs);

// Comparator function for sorting events by uncertainty in ascending order
int event_uncertainty_asc(const void *lhs, const void *rhs);

//Comparitor function for sorting events by remaining_time (e.g., elasticity) in descending order
int event_remaining_time_desc(const void *lhs, const void *rhs);

/**
  * scheduling sc by uncertainty
  */

#ifdef TINTIN_TOTAL_UNCERTAINTY_POLICY
//Turning off for now since this would have to be fixed
void total_uncertainty_fast(struct tintin_event **scs, const int len);

void total_uncertainty(struct tintin_event **scs, const int len, int m);
#endif

u64 tintin_schedule_uncertainty_first(struct tintin_event **scs, const int len,
				      const int m, u64 period);
u64 tintin_schedule_rr(struct tintin_event **scs, const int len, const int m,
		       u64 period);
u64 tintin_schedule(struct tintin_event **scs, const int len, const int m,
		    u64 period);

#endif //TINTIN_SCHEDULER_H