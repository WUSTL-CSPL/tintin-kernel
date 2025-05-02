
#include <linux/sort.h>
// #include <linux/kernel.h>

#include <linux/tintin_event.h>
#include "tintin_scheduler.h"

// #define TINTIN_ELASTIC_DEBUG 

#define US_SCALE 1000

extern u64 tintin_sched_interval_ms;


#ifdef TINTIN_TOTAL_UNCERTAINTY_POLICY
//Turning off for now since this would have to be fixed
/*
 * void total_uncertainty_fast(struct Event * events, const int len)
 * Solve the total uncertainty problem for m = 1, linear on len
 * Inputs:
 *  struct Event * events: an array of events
 *  const int len: the length of the events array
 */
void total_uncertainty_fast(struct tintin_event **scs,
			    const int len)
{
	// Compute square roots of weights
	int sum = 0;
	int i;

	for (i = 0; i < len; ++i) {
		scs[i]->utilization = US_SCALE * int_sqrt(scs[i]->uncertainty);
		sum += scs[i]->utilization;
	}

	int sqrt_c = (NUM_HPCS * US_SCALE) / sum;

	// Assign utilizations
	for (i = 0; i < len; ++i) {
		scs[i]->utilization *= sqrt_c;
	}
}

/*
 * void total_uncertainty(struct Event * events, const int len, const int m)
 * Solve the total uncertainty problem for arbitrary positive m, quasilinear on length
 * Inputs:
 *  struct Event * events: an array of events
 *  const int len: the length of the events array
 *  int m: the number of counters
 */
void total_uncertainty(struct tintin_event **scs, const int len,
		       int m)
{
	int i;
	struct tintin_event *sc;
	struct perf_event *perf_e; // TO PRINT perf_event data
	int sum = 0;
	int sqrt_c, u;

	sort(scs, len, sizeof(struct tintin_event *),
	     &event_uncertainty_desc, NULL);

#ifdef TINTIN_ELASTIC_DEBUG
	/* For debug purpose */
	printk("[Tintin-Uncertainty] After scheduling:");
	for (i = 0; i < len; i++) {
		sc = scs[i];
		perf_e = sc->_perf_event;
		printk("[Tintin-Uncertainty] the event at addr 0x%p, namly %llu has the uncertainty: %d",
		       perf_e, perf_e->attr.config, sc->uncertainty);
	}
#endif

	// Compute square roots of weights

	for (i = 0; i < len; ++i) {
		sc = scs[i];

		// Unless it will  crash
		if (sc->uncertainty == 0) {
			sc->uncertainty = 1;
		}
		sc->utilization = int_sqrt(sc->uncertainty);
		sum += scs[i]->utilization;
	}

	if (sum == 0) {
		printk("[Tintin-Sched] Sum is Zero");
		return;
	}

	sqrt_c = (NUM_HPCS * US_SCALE * tintin_sched_interval_ms) / sum;

	// Assign utilizations 
	for (i = 0; i < len; ++i) {

		// Assign the priority here
		// Less means higher priority
		scs[i]->priority = i + 1; // + 1 to avoid 0 value
		// End - Assign priority


		u = scs[i]->utilization * sqrt_c;
		if (u > US_SCALE * tintin_sched_interval_ms) {
			u = US_SCALE * tintin_sched_interval_ms;
			sc = scs[i];
			perf_e = sc->_perf_event;

#ifdef TINTIN_ELASTIC_DEBUG			
			printk("[Tintin-Uncertainty] the event at addr 0x%p, namely %llu has Uncertainty: %d and Utilization: %d",
			       perf_e, perf_e->attr.config, sc->uncertainty, u);
#endif

			m -= 1;
			sum -= scs[i]->utilization;

			// To avoid divide by zero
			if (sum == 0) {
				printk("[Tintin-Uncertainty] Sum is Zero in the loop");
				return;
			}

			sqrt_c = (NUM_HPCS * US_SCALE *
				  tintin_sched_interval_ms) /
				 sum;
		}
		scs[i]->utilization = u;
	}
}
#endif // TINTIN_TOTAL_UNCERTAINTY_POLICY



/*
 * Running standard-deviation using Welford's method, based on:
 * https://jonisalonen.com/2013/deriving-welfords-method-for-computing-variance/
 */

// Translate float weight to int
// int add(int curr_mean, u64 val, int time_weight) {
// 	int mean;
// 	total_time_weight += time_weight;
// 	total += val * time_weight;

// 	mean = total / total_weight;
// 	var += weight * (val - mean) * (val - old_mean);
// ​
// 	computed = false;
// }
// ​
// int remove(int curr_mean, u64 val, int weight) {
// 	total_weight -= weight;
// 	total -= val * weight;
// 	float old_mean = mean;
// 	mean = total / total_weight;
// 	var -= weight * (val - mean) * (val - old_mean);
// }

inline void tintin_update_uncertainty(
	struct tintin_event * sc)
{
	sc->uncertainty = int_sqrt64(sc->Welfords_variance) * sc->total_time_unmonitored;
}

void tintin_update_variance_by_Welfords_method(
	struct tintin_event * sc,
	u64 count, u64 time)
{

	//Calculate variance in rate, weighted by time
	//Rate is count/time

	// struct tintin_vector *tv = sc->count_vec;
	// struct tintin_measurement *m_curr;
	u64 rate, old_mean;

	// @FIXME TMP solution
	// If an event is pinned, we give it the highest uncertainty
	if (sc->pinned) {
		sc->Welfords_variance = U64_MAX;
		return;
	}


	//Note: this function is only called if the sc was updated
	//Therefore, the following check shouldn't be needed,
	//but we have it just in case.
	//We can do some printk statements to check if it's ever triggered
	// if (tv->size < 1) return;

	// m_curr = tintin_vector_get_measurement_by_index(tv, 0);

	//Avoid divide by 0 or overflow (if unsigned, can't be less than 0)
	//Should never happen, since we also check before pushing into the vector
	// if (m_curr->on_HPC_time <= 0) return; 
	if (time == 0) return;

	//Update values for variance	
	// rate = m_curr->count * SCALE_RATE_FOR_VARIANCE / m_curr->on_HPC_time; //Value not used due to rounding issues
	rate = count * SCALE_RATE_FOR_VARIANCE / time;
	old_mean = sc->Welfords_mean;
	// sc->Welfords_total += m_curr->count * SCALE_RATE_FOR_VARIANCE;
	sc->Welfords_total += count * SCALE_RATE_FOR_VARIANCE;
	// sc->Welfords_total_weight = sc->total_time_monitored; //Should not be 0
	sc->Welfords_total_weight += time;
	sc->Welfords_mean = sc->Welfords_total / sc->Welfords_total_weight;

	//If this is the first counted sample, val - sc->Welfords_mean is 0
	//So variance_running and variance are correctly set to 0
	// sc->Welfords_variance_running += m_curr->on_HPC_time *
	// 						(rate - sc->Welfords_mean) *
	// 						(rate - old_mean);
	sc->Welfords_variance_running += time *
							(rate - sc->Welfords_mean) *
							(rate - old_mean);

	sc->Welfords_variance = sc->Welfords_variance_running / sc->Welfords_total_weight;

}

/*
TODO msudvarg: need to reimplement.
Not using sliding method for now,
just for debugging purposes
void tintin_update_variance_by_Welfords_method_sliding(
	struct tintin_event *sc)
{
	// Conversion is not necessary any more
	struct tintin_vector *tv = sc->count_vec;
	struct tintin_measurement *m_remove, *m_remove_prev;
	struct tintin_measurement *m_curr, *m_prev;
	int val, time_weight;
	int old_mean;
	int stdvar;

	if (tv->size < 2) {
		sc->uncertainty = 0;
		return;
	}

	// This method still has problem

	m_curr = tintin_vector_get_measurement_by_index(tv, 0);
	m_prev = tintin_vector_get_measurement_by_index(tv, 1);

	// Get the count per unit

	// To avoid divide by zero
	if (m_curr->scheduled_out_time - m_prev->scheduled_out_time == 0) {
		printk("[Tintin-Welfords] Two samples have the same timestamp");
		return;
	}

	if (m_curr->on_HPC_time == 0) {
		printk("[Tintin-Welfords] on_HPC_time is 0");
		return;
	}
	val = (m_curr->count / m_curr->on_HPC_time) *
	      (m_curr->scheduled_out_time - m_prev->scheduled_out_time);
	time_weight = SCALE_TO_PERCENTAGE * m_curr->on_HPC_time /
		      (m_curr->scheduled_out_time - m_prev->scheduled_out_time);

	// Add a new count
	sc->Welfords_time_weight += time_weight;
	sc->total_count += m_curr->count; // This is (val * time_weight)
	old_mean = sc->mean_count;

	// To avoid divide by zero
	if (sc->Welfords_time_weight == 0) {
		// sc->Welfords_time_weight = 1;
		printk("[Tintin-Welfords] Welfords_time_weight is 0");
		return;
	}
	sc->mean_count = sc->total_count / sc->Welfords_time_weight;
	sc->Welfords_variance +=
		time_weight * abs(val - sc->mean_count) * abs(val - old_mean);

	// Remove the oldest count
	if (tv->size > tintin_count_window_size) {
		m_remove = tintin_vector_get_measurement_by_index(
			tv, tintin_count_window_size);
		m_remove_prev = tintin_vector_get_measurement_by_index(
			tv, tintin_count_window_size + 1);

		if (m_remove->on_HPC_time == 0) {
			printk("[Tintin-Welfords] on_HPC_time (in removing) is 0");
			return;
		}
		val = (m_remove->count / m_remove->on_HPC_time) *
		      (m_remove->scheduled_out_time -
		       m_remove_prev->scheduled_out_time);
		// To avoid divide by zero
		if (m_remove->scheduled_out_time -
			    m_remove_prev->scheduled_out_time ==
		    0) {
			printk("[Tintin-Welfords] Two samples have the same timestamp");
			return;
		}
		time_weight = SCALE_TO_PERCENTAGE * m_remove->on_HPC_time /
			      (m_remove->scheduled_out_time -
			       m_remove_prev->scheduled_out_time);

		sc->Welfords_time_weight -= time_weight;
		sc->total_count -= m_remove->count;
		old_mean = sc->mean_count;
		// To avoid divide by zero
		if (sc->Welfords_time_weight == 0) {
			sc->Welfords_time_weight = 1;
			printk("[Tintin-Welfords] Welfords_time_weight (in removing) is 0");
			return;
		}
		sc->mean_count = sc->total_count / sc->Welfords_time_weight;
		sc->Welfords_variance -= time_weight *
					 abs(val - sc->mean_count) *
					 abs(val - old_mean);
	}

	// Update the uncertainty
	// To avoid divide by zero
	if (sc->Welfords_time_weight == 0) {
		sc->Welfords_time_weight = 1;
		printk("[Tintin-Welfords] Welfords_time_weight (in updating uncertainty) is 0");
		return;
	}
	stdvar = sc->Welfords_variance / sc->Welfords_time_weight;

	if (stdvar <= 0) {
		printk("[Tintin-Welfords] Error std variance is <= 0");
		sc->uncertainty = 0;
		return;
	}

	sc->uncertainty = int_sqrt(stdvar);

	// #ifdef TINTIN_EVENT_HANDLER_DEBUG
	printk("[Tintin-Welfords] The uncertainty variance is %d",
	       sc->uncertainty);
	// #endif

	return;
}
*/

#ifdef TINTIN_UNCERTAINTY_TRIANGLE
void tintin_update_uncertainty_by_triangle(struct tintin_event *sc)
{
	struct tintin_vector *tv;
	struct tintin_measurement *m_curr, *m_prev;

	u64 uncertainty_area; // For comparison

	tv = (struct tintin_vector *)sc->count_vec;

	// No enough information for calculating the uncertainty
	if (tv->size < 2) {
		sc->uncertainty = 0;
		return;
	}

	/*
 * The calculation is based on
                    0
				   00
				  000
				 0000		   
			    00000
			   000000
              000000-
             0000----
	        ---------
		-------------
	-----------------
	t2      t1      t0

 * if we use the count value between t2 and t1 to extrapolate, the area would be '-'
 * however, the actual value should be '-' + '0'. 
 * Here the area of '0' is the uncertainty quantified by the triangle method
 */

	m_curr = tintin_vector_get_measurement_by_index(tv, 0);
	m_prev = tintin_vector_get_measurement_by_index(tv, 1);

	// To avoid divide by zero
	if (m_prev->on_HPC_time == 0) {
		m_prev->on_HPC_time = 1;
	}

	uncertainty_area =
		abs(m_curr->count - (m_prev->count * m_curr->on_HPC_time /
				     m_prev->on_HPC_time));

	// To avoid divide by zero
	if (m_prev->count == 0) {
		m_prev->count = 1;
	}

	// scale to 10 times, this is a magic value
	sc->uncertainty = (uncertainty_area * 10) / m_prev->count;
	// printk("[Tintin] Uncertainty is : %d", sc->uncertainty);

	// Remove historical data out of the time window if needed
	// TO CHECK: Actually, this is not necessary if we use ring buffer now
	while (tv->size >
	       tintin_count_window_size) { // count_window_size is global variable
		tintin_vector_pop_back(tv);
		printk_once(KERN_WARNING "[Tintin] Vector size is : %lu",
			    tv->size);
	}
}
#endif // TINTIN_UNCERTAINTY_TRIANGLE
