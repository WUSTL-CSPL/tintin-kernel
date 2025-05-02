// #include "internal.h"



#include <linux/sort.h>
#include "tintin_estimation.h"
#include <linux/tintin_event.h>
#include "tintin_scheduler.h"



#define E_SCALE 1000ULL

extern u64 tintin_sched_quantum;

/*
 * void elastic(struct Event * events, const int len, int m)
 * Solve the elastic problem for arbitrary positive m, quasilinear on length
 * Inputs:
 *  struct Event * events: an array of events
 *  const int len: the length of the events array
 *  int m: the number of counters
 */
void tintin_elastic(struct tintin_event** scs, const int len, int m, u64 timeslice) {

    struct tintin_event * sc;

    int n_timeslices;
    u64 u_min, u_max, u_bound, u_sum, e_sum, delta, i, e, compression, now, weight, count;    

    n_timeslices = (len % m) ? (len/m + 1) : len/m;
    u_max = n_timeslices * timeslice; // Desired maximum utilization for each event, equal to the hyperperiod
    // TODO Ask Marion about this
    // u_max = timeslice; // Desired maximum utilization for each event, equal to the hyperperiod
    u_bound = u_max * m; // Total utilization bound
    u_sum = u_max * len; // Total utilization demand

    // u_min = timeslice/U_MIN_RATIO; // Minimum utilization to prevent events from being starved

    u_min = tintin_sched_quantum; // Changed to uses

    //No need to multiplex
    if(len <= m) {
        for (i = 0; i < len; ++i) {
            sc = scs[i];            
            sc->remaining_time = u_max;
        }
        return;
    }

    e_sum = 0; // Total elasticity
    delta = 0; // Total utilization allocated to tasks already at minimum utilization
    
	now = local_clock(); // Get the current time for count extrapolation

    // Assign elasticities to the SC
    for (i = 0; i < len; ++i) {
        sc = scs[i];

        // If weight is 0, then elasticity will be infinite. Just make it the max value
        weight = sc->Welfords_variance*sc->weight;
        
        // printk ("[Tintin-Elastic] Event's weight is : %u", sc->weight);
        if (weight > 0) {
            // E_SCALE helps avoid rounding issues
            // scheduler only invoked on tintin_mux_hrtimer_handler,
            // so perf_event_update_time has already been called 
            count = tintin_running_count_extrapolated(sc);

            // Could cause overflow
            if(count > 1000000000ULL) {
                count = count/1000ULL;
                weight = weight/1000000ULL;
                weight = weight?weight:1;
            }
            e = count * count / weight;
            //e = E_SCALE*SCALE_RATE_FOR_VARIANCE*tintin_running_count_extrapolated(sc)/weight;  
            e_sum += e;  
        } else {
            e = U64_MAX; //Does not contribute to e_sum
        }

        // Temporarily use remaining_time to store elasticity
        sc->remaining_time = e;
    }

    //If e_sum is 0, then every event gets equal share
    if (e_sum == 0) {       
        for (i = 0; i < len; ++i) {
            sc = scs[i];            
            sc->remaining_time = u_bound/len;
        }
        return; 
    }

    // Sort the SC array by elasticity descending. More elastic means faster to reach u_min
    sort(scs, len, sizeof(struct tintin_event*), &event_remaining_time_desc, NULL);
    
    for (i = 0; i < len; ++i) {

        sc = scs[i];
        e = sc->remaining_time;

        //Cannot divide by zero; already dealt with 0 e_sum above
        compression = (u_sum - u_bound + delta) * e / e_sum;
        if(compression > u_max - u_min) {
            //Event i utilization would be compressed below u_min
            delta += u_min;
            u_sum -= u_max;            
            if (e < e_sum) e_sum -= e;
            sc->remaining_time = u_min;
        }
        else {
            sc->remaining_time = u_max - compression;
        }

    }

}