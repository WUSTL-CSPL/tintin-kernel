#ifndef TINTIN_VECTOR_H
#define TINTIN_VECTOR_H

#include <linux/kernel.h>
#include <linux/slab.h>


enum event_type {
    LINUX_EVENT,
    TINTIN_EVENT
};

struct tintin_measurement {
    u64 count; //the event count during the measurement interval

    u64 on_HPC_time; //the duration over which the count was obtained (in ns)
    u64 scheduled_out_time; //the end time of the measurement (in ns)

    /*
     * Total time (since the start of monitoring) the event was enabled when
     * this measurement was taken (in ns)
     */
    u64 total_time_enabled;

    /*
     * Total time (since the start of monitoring) the event was running
     * (i.e. being measured) when this measurement was taken (in ns)
     */
    u64 total_time_running;
};


// Structure definition
struct tintin_vector {
    struct tintin_measurement* measurements;
    // u64 running_count;
    // int uncertainty; // May need to remove this
    int front;
    int back;
    // int mean;
    size_t capacity;  // Total capacity
    size_t size;  // Current number of elements
};

// Function prototypes

// Initialize the tintin_vector
int tintin_vector_init(struct tintin_vector *tv, size_t initial_capacity);

void tintin_vector_del(struct tintin_vector *tv);

// Push a new measurement to the front of the vector
int tintin_vector_push_front(struct tintin_vector *tv, u64 count, u64 on_hpc_time,
			     u64 scheduled_out_time, u64 total_time_enabled,
			     u64 total_time_running);

// Push a new measurement to the back of the vector
int tintin_vector_push_back(struct tintin_vector *tv, u64 count,
			    u64 on_hpc_time,
			    u64 scheduled_out_time, u64 total_time_enabled,
			    u64 total_time_running);

// Get a measurement from the vector by index
struct tintin_measurement* tintin_vector_get_measurement_by_index(struct tintin_vector *tv, int index);

// Pop a measurement from the back of the vector
struct tintin_measurement* tintin_vector_pop_back(struct tintin_vector *tv);

// Pop a measurement from the front of the vector
struct tintin_measurement* tintin_vector_pop_front(struct tintin_vector *tv);


#endif // TINTIN_VECTOR_H
