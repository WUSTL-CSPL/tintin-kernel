#include "tintin_estimation.h"
#include <linux/kernel.h>

//Returns interpolated time from beginning of the event to the end of the first measurement
u64 tintin_TAM_first_measurement(u64 count, u64 time_enabled, u64 time_monitored){
	/* Special case.
	 * We want to estimate the event count that happened during t=[0, event scheduled in), and add
	 * that to the count during t = [event scheduled in, event scheduled out]. To achieve that,
	 * we assume that the rate of count during the first interval is the same as the second interval.
	 */

	/*
	 * To obtain the time between when we started monitoring and when the first measurement was started,
	 * we can take the unmonitored time.
	 */
	return (time_enabled * count) / time_monitored;
}

//Returns interpolated time from end of the previous measurement to the end of the current measurement
u64 tintin_TAM(u64 curr_count, u64 curr_time_enabled, u64 curr_hpc_time,
	       u64 prev_count, u64 prev_time_enabled, u64 prev_hpc_time){
	u64 trapezoid_width, trapezoid_area;

	/*
	 * want: trapezoid_area = (rate_prev + interpolated_rate) * trapezoid_width / 2;
	 *                      where interpolated_rate = rate_prev +- rate_slope * trapezoid_width
	 *
	 * so trapezoid_area = (rate_prev + rate_prev +- abs_rate_slope * trapezoid_width) * trapezoid_width / 2
	 *                   = (2*rate_prev +- abs_rate_slope * trapezoid_width) * trapezoid_width / 2                         (simplify rate_prev + rate_prev)
	 *                   = ( (2*prev_count)/prev_hpc_time +- abs_rate_slope * trapezoid_width) * trapezoid_width / 2       (2 * rate_prev = (2*prev_count)/prev_hpc_time)
	 *                   = [ (2*prev_count * trapezoid_width)/prev_hpc_time +- abs_rate_slope * trapezoid_width^2 ] / 2    (distribute trapezoid_width)
	 *                   = [ (2*prev_count * trapezoid_width)/prev_hpc_time +- (abs_rate_change / (midpoint_curr - prev_scheduled_out_time)) * trapezoid_width^2 ] / 2  (replace abs_rate_slope with its definition)
	 *                   = [ (2*prev_count * trapezoid_width)/prev_hpc_time +- ((abs_rate_	change * trapezoid_width^2) / (midpoint_curr - prev_scheduled_out_time)) ] / 2 (move * trapezoid_width^2 to happen before division)
	 *
	 * This rearrangement minimizes rounding error due to division and it should not lead to overflow
	 */

	//I've done a bit more rearranging here to get rid of more rounding errors
	//and it conveniently doesn't care if rate cNhange is positive or negative

	//Correctly computes total time (monitored plus unmonitored)
	//over which we will interpolate since last measurement

	trapezoid_width = curr_time_enabled - prev_time_enabled;
	trapezoid_area = (prev_count * trapezoid_width / prev_hpc_time +
			  ((trapezoid_width * trapezoid_width)/curr_hpc_time) * curr_count /
				  (2ULL * trapezoid_width - curr_hpc_time)) -
			 ((trapezoid_width * trapezoid_width)/prev_hpc_time) * prev_count / 
				 (2ULL * trapezoid_width - curr_hpc_time);
	return trapezoid_area;
}


//Returns only interpolated time, need to add sc->running_count to result
/*
 * Given a tintin vector with two measurements, one from time [a, b] and one from time
 * [c, d], this function returns the estimated count from time (b, d]. If only one event is present,
 * it returns the estimated count from [0, b].
 *
 * The interpolation is based on the Trapezoid Area Method (TAM) described in:
 * Mathur, Wiplove, and Jeanine Cook.
 * "Toward accurate performance evaluation using hardware counters."
 * ITEA Modeling and Simulation Workshop. 2003.
 *
 * We modify the paper's implementation slightly:
 * 	- Instead of using # of cycles across measurements to measure the rate of an
 * 	event, we use time. (Thus rate becomes count/time instead of count/cycles).
 * 	- We compute the change in the rate from the ending of the first measurement (t=b) to the
 * 	midpoint of the second measurement (t=(d-c)/2). This way, we avoid over/under estimating the second
 * 	measurement.
 */
u64 tintin_interpolate_count_by_TAM(struct tintin_event *sc)
{
	struct tintin_vector *tv;
	struct tintin_measurement *m_curr, *m_prev;
	u64 trapezoid_area;

	tv = sc->count_vec;
	if (tv->size == 0){
		return 0;
	}

	m_curr = tintin_vector_get_measurement_by_index(tv, 0);

	if (tv->size == 1){
		// printk(KERN_INFO "[Tintin-Interpolation] Tintin event=%lld First vector element "
		// 	"current: measured count %llu "
		// 	"total time enabled %llu total time monitored %llu\n",
		// 	sc->_perf_event->id, m_curr->count, sc->total_time_enabled, sc->total_time_monitored);

		return tintin_TAM_first_measurement(m_curr->count, sc->total_time_enabled, sc->total_time_monitored);
	}

	m_prev = tintin_vector_get_measurement_by_index(tv, 1);

	trapezoid_area = tintin_TAM(m_curr->count, m_curr->total_time_enabled, m_curr->on_HPC_time,
				    m_prev->count, m_prev->total_time_enabled, m_prev->on_HPC_time);

	// printk(KERN_INFO "[Tintin-Interpolation] "
	// 	"Tintin event=%lld curr_count=%llu curr_total_time_enabled=%llu curr_hpc_time=%llu prev_count=%llu prev_total_time_enabled=%llu prev_hpc_time=%llu interpolated_value=%llu \n",
	// 	sc->_perf_event->id, m_curr->count, m_curr->total_time_enabled, m_curr->on_HPC_time, m_prev->count, m_prev->total_time_enabled, m_prev->on_HPC_time, trapezoid_area);

	return trapezoid_area;
}

//Extrapolates from end of last measurement, no need to add sc->running_count
/*
 * Given a tintin vector where its last measurement was scheduled out at
 * time t1, this method estimates the count during time (t1, end_time]
 * and returns the running count from [0, end_time].
 */
u64 tintin_running_count_extrapolated(struct tintin_event *sc)
{
	
	struct tintin_vector *tv;
	struct tintin_measurement *last_measurement;
	//int index;
	//u64 rate;
	u64 time_diff, extrapolated_count;

	tv = sc->count_vec;
	if (tv->size == 0){
		// printk(KERN_WARNING "tintin_extrapolate_count: Tintin vector is empty. Returning 0\n");
		return 0;
	}

	//index = 0;
	last_measurement = tintin_vector_get_measurement_by_index(tv, 0);

// This is for debugging
/*
	while (last_measurement->scheduled_out_time > end_time){
		if (index >= tv->size){
			printk(KERN_ALERT "tintin_extrapolate_count: Could not find measurement before end time. Returning 0");
			return 0;
		}

		printk(KERN_ALERT "tintin_extrapolate_count: End time is less than scheduled out time of last measurement");
		index++;
		last_measurement = tintin_vector_get_measurement_by_index(tv, index);
	}
*/
// End of debugging

	//Rounding errors handled better:
	time_diff = sc->_perf_event->total_time_enabled - last_measurement->total_time_enabled;
	extrapolated_count = (time_diff * last_measurement->count) / last_measurement->on_HPC_time;
	// printk(KERN_INFO "[Tintin-Extrapolation] Event %p %lld old count %llu extrapolated_count %llu",
	//  	sc, sc->_perf_event->id, sc->running_count, extrapolated_count);
	return sc->running_count + extrapolated_count;
	
}

/*
Find out the current uncertainty
*/
u64 tintin_uncertainty_extrapolated(struct tintin_event *sc)
{
	int uncertainty = 0;
	struct perf_event* event = sc->_perf_event;

	u64 unmonitored_time = event->total_time_enabled - event->total_time_running;
	uncertainty = int_sqrt(sc->Welfords_variance) * unmonitored_time / SCALE_RATE_FOR_VARIANCE;
	return uncertainty;
}