#include "linux/tintin_vector.h"

// #define TINTIN_VECTOR_DEBUG

// TODO export to /proc/sys
int tintin_count_window_size = 10;
EXPORT_SYMBOL(tintin_count_window_size);

int tintin_vector_init(struct tintin_vector *tv, size_t initial_capacity)
{
	tv->measurements =
		kmalloc(initial_capacity * sizeof(struct tintin_measurement),
			GFP_KERNEL);

	if (!tv->measurements) {
		return -ENOMEM;
	}
	// tv->running_count = 0;
	tv->capacity = initial_capacity;
	tv->size = 0;
	tv->front = 0; // Initialize the front of the ring buffer
	tv->back = 0;
	// tv->mean = 0;
	return 0;
}

void tintin_vector_del(struct tintin_vector *tv)
{
    if (tv && tv->measurements) {
        kfree(tv->measurements);
        tv->measurements = NULL;
    }

    tv->capacity = 0;
    tv->size = 0;
    tv->front = 0;
    tv->back = 0;
    // Reset other members to their initial state if needed
}

int tintin_vector_push_front(struct tintin_vector *tv, u64 count,
			     u64 on_hpc_time,
			     u64 scheduled_out_time, u64 total_time_enabled,
			     u64 total_time_running)
{
	int index;
	if (tv->size == tv->capacity) {
		// Ring buffer is full, we need to overwrite the oldest data
		tv->size--;
	}


	// Calculate the index for the new data
	index = tv->front;

	tv->measurements[index].count = count;
	tv->measurements[index].on_HPC_time = on_hpc_time;
	tv->measurements[index].scheduled_out_time = scheduled_out_time;
	tv->measurements[index].total_time_enabled = total_time_enabled;
	tv->measurements[index].total_time_running = total_time_running;
	
	tv->front = (tv->front + 1) % tv->capacity;

	tv->size++;

	return 0;
}

int tintin_vector_push_back(struct tintin_vector *tv, u64 count,
			    u64 on_hpc_time,
			    u64 scheduled_out_time, u64 total_time_enabled,
			    u64 total_time_running)
{
	int index;
	if (tv->size == tv->capacity) {
		// Ring buffer is full, we need to overwrite the oldest data
		tv->size--;
	}

	// Calculate the index for the new data at the back
	index = (tv->back + tv->size) % tv->capacity;

	tv->measurements[index].count = count;
	tv->measurements[index].on_HPC_time = on_hpc_time;
	tv->measurements[index].scheduled_out_time = scheduled_out_time;
	tv->measurements[index].total_time_enabled = total_time_enabled;
	tv->measurements[index].total_time_running = total_time_running;
	tv->size++;

	return 0;
}

struct tintin_measurement *
tintin_vector_get_measurement_by_index(struct tintin_vector *tv, int index)
{
	int buffer_index;
	index = index + 1;

	if (index <= 0 || index > tv->size) {
		// Index is out of bounds
		return NULL; // You may want to choose a suitable error value
	}

	// Calculate the actual index within the ring buffer
	buffer_index = (tv->front + tv->capacity - index) % tv->capacity;

#ifdef TINTIN_VECTOR_DEBUG
	printk("[Tintin-Vector] Front is %d | Buffer idx is %d ", tv->front,
	       buffer_index);
#endif
	// Get the element at the specified index
	return &tv->measurements[buffer_index];
}

struct tintin_measurement *tintin_vector_pop_back(struct tintin_vector *tv)
{
	struct tintin_measurement *item_to_return;
	if (tv->size == 0) {
		return 0; // Vector is empty
	}
	// Get the data from the front of the ring buffer
	item_to_return = &tv->measurements[tv->back];

	// Move the front pointer to the next element
	tv->back = (tv->back + 1) % tv->capacity;
	tv->size--;

	return item_to_return;
}


struct tintin_measurement *tintin_vector_pop_front(struct tintin_vector *tv)
{
	struct tintin_measurement *item_to_return;
	if (tv->size == 0) {
		return NULL; // Vector is empty
	}

	// Get the data from the front of the ring buffer
	item_to_return = &tv->measurements[tv->front];

	// Move the front pointer to the next element
	tv->front = (tv->front + 1) % tv->capacity;
	tv->size--;

	return item_to_return;
}

void tintin_vector_free(struct tintin_vector *tv)
{
	kfree(tv->measurements);
	tv->size = 0;
	tv->capacity = 0;
	tv->front = 0;
	tv->back = 0;
	// tv->running_count = 0;
	// tv->mean = 0;
}