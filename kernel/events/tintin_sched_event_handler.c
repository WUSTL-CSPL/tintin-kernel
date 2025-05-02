
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tintin_event.h>
#include "tintin_scheduler.h"
#include "tintin_utils.h"
#include "tintin_estimation.h"
#include "tintin_uncertainty.h"
#include <linux/tintin_vector.h>
#include "tintin_profiling_context.h"
#include <linux/minmax.h>
/**
 *  Turn on/off Tintin Debugging
*/

// #define TINTIN_EVENT_HANDLER_DEBUG


/** 
 * Test if the removement is okay
*/

// #define TINTIN_EVENT_REMOVE_DEBUG
int remove_debug_count = 0;

/**
 *  Turn on/off Tintin scheduling
*/
int tintin_switch_on;
EXPORT_SYMBOL(tintin_switch_on);

extern int tintin_count_window_size;
extern u64 tintin_sched_interval_ms;

u64 tintin_sched_remaining_time;
EXPORT_SYMBOL(tintin_sched_remaining_time);
/**
 * Variables
*/

// This variable indicate the length of Tintin event list
int tintin_sc_index = 0;
// This list holds all tintin_related events
struct tintin_event *tintin_sc_list[MAX_NUM_TINTIN_SC];
EXPORT_SYMBOL(tintin_sc_list);

// Exported to /proc/sys
u64 tintin_sched_quantum;
EXPORT_SYMBOL(tintin_sched_quantum);
/*
 scheduling policy
 0. Elastic
 1. Round robin
 2. Uncertainty-First
 Default: Elastic
*/
int tintin_scheduling_policy;
EXPORT_SYMBOL(tintin_scheduling_policy);

struct list_head tintin_esx_list;
EXPORT_SYMBOL(tintin_esx_list);


#define EVAL_BAYESPERF
int defined_bayesperf;
EXPORT_SYMBOL(defined_bayesperf);

#ifdef EVAL_BAYESPERF
////////// The following functions are added to emulate the EP update
////////// Some of them are scaled up to int, avoiding floating point operations
// Gaussian struct for mean and variance
typedef struct {
    int mean;
    int variance;
} Gaussian;

// Function to compute the product of two Gaussian distributions
Gaussian multiply_gaussians(Gaussian a, Gaussian b) {
    Gaussian result;
	int precision_a, precision_b;
	int mean_post, var_post, precision_post;

	// Calculate the precision (inverse of variance)
	precision_a = 100000 / (a.variance > 0 ? a.variance : 1);
	precision_b = 100000 / (b.variance > 0 ? b.variance : 1);

    precision_post = precision_a + precision_b;
	precision_post = precision_post > 0 ? precision_post : 1;
    mean_post = (a.mean * precision_a + b.mean * precision_b) / precision_post;
    var_post = 100000 / precision_post;
	var_post = var_post > 0 ? var_post : 1;

	result.mean = mean_post;
	result.variance = var_post;
	return result;
}

// Function to simulate one iteration of EP update
Gaussian ep_update(void) {
	Gaussian prior = {0, 1};                 // Prior N(0,1)
	Gaussian site = {0, 1e6};                  // Initial site (almost no influence)
	Gaussian cavity;
	Gaussian true_likelihood = {10, 5};       // Observed data N(1, 0.5)

	
	// Step 1: Remove site from approximate posterior -> cavity distribution	
	Gaussian updated, posterior;
	int new_site_precision, new_site_mean;
    int cavity_precision = 1000000 / prior.variance - 1000000 / site.variance;
    int cavity_mean = (prior.mean / prior.variance - site.mean / site.variance) / cavity_precision;

    cavity.mean = cavity_mean;
    cavity.variance = 1000000 / (cavity_precision > 0 ? cavity_precision : 1);
	cavity.variance = cavity.variance > 0 ? cavity.variance : 1;


    // Step 2: Multiply cavity with true likelihood (moment-matching would go here)
    updated = multiply_gaussians(cavity, true_likelihood);

    // Step 3: Update site to match updated moments
    new_site_precision = 1000000 / updated.variance - 1000000 / cavity.variance;
    new_site_mean = (updated.mean / updated.variance - cavity.mean / cavity.variance) / new_site_precision;

    site.mean = new_site_mean;
    site.variance = 1000000 / (new_site_precision > 0 ? new_site_precision : 1);
	site.variance = site.variance > 0 ? site.variance : 1;

    // Step 4: Update approximate posterior
    posterior = multiply_gaussians(cavity, site);
    return posterior;
}
#endif // End EVAL_BAYESPERF
// End Tintin


/**
 * Main scheduling function
*/

void tintin_update_events(void)
{
	printk("[Tintin] I am updating the event counts");
}

/**
 * We integrate this into the event update functions
void tintin_update_uncertainty(void)
{
	printk("[Tintin] I am updating the event uncertainty");
}
*/

/*
 * Compare function for red black tree 
 * key (see perf_event_groups_less). This places it last inside the CPU
 * subtree.
 */

#define __node_2_pe(node) rb_entry((node), struct perf_event, group_node)

static inline bool __group_index_less(struct rb_node *a,
				      const struct rb_node *b)
{
	struct perf_event *e_left = __node_2_pe(a);
	struct perf_event *e_right = __node_2_pe(b);

	return e_left->group_index > e_right->group_index ? 1 : 0;
}

/*
 * Insert @event into @groups' tree; using {@event->cpu, ++@groups->index} for
 * key (see perf_event_groups_less). This places it last inside the CPU
 * subtree.
 */
static void tintin_insert_event_into_rbtree(struct perf_event_groups *groups,
					    struct perf_event *event)
{
	event->group_index = ++groups->index;

	if (event->event_tp == TINTIN_EVENT) {
		// Use the uncertainty as a value
		struct tintin_event *sc =
			(struct tintin_event *)event->tintin_esx;
		event->group_index = sc->priority;
	}

	rb_add(&event->group_node, &groups->tree, __group_index_less);
}

/*
 * Helper function to initialize event group nodes.
 */
static void tintin_reset_event_group_node(struct perf_event *event)
{
	RB_CLEAR_NODE(&event->group_node);
	event->group_index = 0;
}

/* Tintin's code
 * Delete all group from the rb tree.
 */
static void perf_event_groups_all_delete(struct perf_event_groups *groups)
{
	// WARN_ON_ONCE(RB_EMPTY_NODE(&event->group_node) ||
	// 	     RB_EMPTY_ROOT(&groups->tree));

	struct rb_node *node;
	struct perf_event *tmp_event;

	for (node = rb_first(&groups->tree); node; node = rb_next(node)) {
		tmp_event = container_of(node, struct perf_event, group_node);
		rb_erase(node, &groups->tree);
		tintin_reset_event_group_node(tmp_event);
	}
}

/*
 * Move @event to the tail of the @ctx's elegible events.
 */
void tintin_ranks_events_on_rb(struct perf_event_context *ctx,
				      struct perf_event *event)
{
	struct perf_event *event_holder;

#ifdef TINTIN_EVENT_HANDLER_DEBUG
	int node_count;
#endif

	// Remove all events
	// Maybe not necessary
	perf_event_groups_all_delete(&ctx->flexible_groups);

#ifdef TINTIN_EVENT_HANDLER_DEBUG
	node_count = count_rb_tree_nodes(&ctx->flexible_groups);
	printk("[Tintin-RB-Tree] Current node number is %d", node_count);
#endif

	list_for_each_entry (event_holder, &current->perf_event_list,
			     owner_entry) {
		if (event_holder->event_tp == TINTIN_EVENT) {
			tintin_insert_event_into_rbtree(&ctx->flexible_groups,
							event_holder);
		}
	}
}


/*
 * This function handlers event scheduling
 * @return fordward time
 */
/*
 * function must be called with interrupts disabled
 */

u64 tintin_mux_hrtimer_handler(struct hrtimer *hr)
{
	struct perf_cpu_context *cpuctx;

	struct tintin_event *sc;
	u64 forward_time;
	struct perf_event * event;
	int i; // For loop iteration
	u64 tintin_sched_interval_ns = tintin_sched_interval_ms * NSEC_PER_MSEC;

#ifdef EVAL_BAYESPERF 
	Gaussian posterior;
	int j, k;
	// printk("Posterior is : %f", posterior.mean);
#endif
			// posterior = ep_update();

	cpuctx = container_of(hr, struct perf_cpu_context, hrtimer);
	forward_time = cpuctx->hrtimer_interval;

	// In case all events have been removed from monitoring
	if(tintin_sc_index == 0) return forward_time;


	// Update event variance
	for (i = 0; i < tintin_sc_index; ++i) {
		sc = tintin_sc_list[i];
		event = sc->_perf_event;
		
		#ifdef EVAL_BAYESPERF
if(defined_bayesperf == 1) {
		for (k = 0; k < sc->count_vec->size; k++) {
			for (j = 0; j < tintin_sc_index; j++) {
				posterior = ep_update();
			}
		}
}
		#endif

		//if(!sc->updated) {
			//TODO msudvarg: may now need to do this for every event, because updated no longer means just from the hrtimer handler
			perf_event_update_time(event);			
			sc->total_time_monitored = event->total_time_running;
			sc->total_time_enabled = event->total_time_enabled;
			sc->total_time_unmonitored = sc->total_time_enabled - sc->total_time_monitored;
		//}

		// printk(KERN_INFO "Event %p time enabled: %llu, running: %llu\n",
		// 	sc, event->total_time_enabled, event->total_time_running);

		if(sc->updated) sc->updated = false;

		tintin_update_uncertainty(sc);
	}

	// Check if the tintin_sched_interval_ms is in right range
	tintin_sched_interval_ns = min(tintin_sched_interval_ns, (u64) DEFAULT_MAX_SCHED_INTERVAL);
	tintin_sched_interval_ns = max(tintin_sched_interval_ns, (u64) 100000);

	// Run scheduler to set priorities
	// printk(KERN_INFO "[Tintin] Invoking scheduler for timeslice %llu\n", ktime_to_ns(cpuctx->hrtimer_interval));
	if(tintin_scheduling_policy == 1) {
		forward_time = tintin_schedule_rr(tintin_sc_list, tintin_sc_index, NUM_HPCS, tintin_sched_interval_ms * NSEC_PER_MSEC);
	} else if (tintin_scheduling_policy == 0) {
	// This is the elastic scheduling 
		forward_time = tintin_schedule(tintin_sc_list, tintin_sc_index, NUM_HPCS,  tintin_sched_interval_ms * NSEC_PER_MSEC);
	} else if (tintin_scheduling_policy == 2) {
		forward_time = tintin_schedule_uncertainty_first(tintin_sc_list, tintin_sc_index, NUM_HPCS, tintin_sched_interval_ms * NSEC_PER_MSEC);
	}


#ifdef TINTIN_USE_LIST_ITERATION
	u64 new_raw_count = 0;

	struct perf_event_context *ctx;
	struct perf_event *event;
	tintin_esx *tintin_event;
	// if (debug_count++ % 31 == 0) {


	/**
		  * Task Context
		  */
	i = 0;
	list_for_each_entry (event, &current->perf_event_list, owner_entry) {
		// printk("[Tintin] Event initialized %d", event->initialized);
		if (event->event_tp == TINTIN_EVENT) {
			sc = (struct tintin_event *)
				     event->tintin_esx;
			i++;
			if(!sc->updated) continue;

#ifdef TINTIN_EVENT_HANDLER_DEBUG
			// printk("[Tintin] SC priority : %d", sc->priority);
			// Pinning a few events on the register
			if (i == 1 || i == 2) {
				sc->uncertainty = 4000;
				continue;
			}
#endif

			// Monitored time: out of those events that were just monitored,
			// we use the longest elapsed time to determine the unmonitored time
			// for all other events			
			elapsed = sc->last_scheduled_out_time - sc->last_scheduled_in_time;
			monitored_time = (elapsed > monitored_time) ? elapsed : monitored_time;

			// This is the method used in Tintin's paper
			tintin_update_variance_by_Welfords_method(sc);

			// This is the simplified method
			//tintin_update_uncertainty_by_triangle(sc);

#ifdef TINTIN_EVENT_HANDLER_DEBUG
			// printk("[Tintin] SC priority : %d", sc->priority);
			// End of Pinning
#endif

		}

		// else {
		// 	tintin_print_event(event, false);
		// }
	}

	//Update unmonitored time for those events that were not monitored
	list_for_each_entry (event, &current->perf_event_list, owner_entry) {
		if (event->event_tp == TINTIN_EVENT) {
			sc = (struct tintin_event *)
				     event->tintin_esx;
			if(sc->updated) sc->updated = false;
			else sc->unmonitored_time += monitored_time;
			tintin_update_uncertainty(sc);
		}
	}
#endif //TINTIN_USE_LIST_ITERATION

	//Invoke scheduler to set priorities


	/**
		  * Browse all Tintin ESCs
		  */

	// list_for_each_entry (tintin_event, &tintin_esx_list,
	// 		     entry) {
	// 	// tintin_print_tintin_esx(tintin_event);
	// 	printk("I am printing Tintin ESC");
	// }

	// }
	//////////////////// End of debugging

#ifdef TINTIN_EVENT_REMOVE_DEBUG
	// For debugging purpose
	if (tintin_sc_index > 1 && debug_count == 0) {
		for (i = 0; i < tintin_sc_index; i++) {
			sc = tintin_sc_list[i];
			printk("[Tintin-Handler] the event at addr: 0x%p , has the uncertainty: %d",
			       sc, sc->uncertainty);
		}

		sc = *tintin_sc_list;
		printk("[Tintin-Handler] !!!! base addr: 0x%p , first element addr: %p",
		       tintin_sc_list, tintin_sc_list[0]);

		tintin_remove_scheduling_context(tintin_sc_list[0]);

		printk("After removement, the events are: ");
		for (i = 0; i < tintin_sc_index; i++) {
			sc = tintin_sc_list[i];
			printk("[Tintin-Handler] the event at addr: 0x%p , has the uncertainty: %d",
			       sc, sc->uncertainty);
		}
		debug_count = 1; // Make this branch only execute once
	}
#endif

	return forward_time;
}

/**
 * Alloc a new Tintin Event Scheduling Context 
*/

tintin_esx *tintin_esx_alloc(void)
{
	tintin_esx *ret = kzalloc(sizeof(tintin_esx), GFP_KERNEL);
	return ret;
}

// INIT_LIST_HEAD(&tintin_esx_list);

/**
 * Init Tintin Event Scheduling Context
*/

/**
 * Add new Tintin ESX into the list
*/

int tintin_esx_add(tintin_esx *tintin_e)
{
	if (list_empty(&tintin_esx_list)) {
		INIT_LIST_HEAD(&tintin_esx_list);
	}

	// list_add_tail(tintin_esx_list) TODO think

	return 0;
}

/**
 * Debug function -- print out the information of an event
 * There are two versions
 * 	- One for perf event
 *  - One for tintin event
*/

void tintin_print_event(struct perf_event *event, bool is_CPU_context)
{
	if (is_CPU_context) {
		printk("-------CPU Context-------\n");
	} else {
		printk("-------Task Context-------\n");
	}

	printk("Event Addr: Ox%p \n", event);
	printk("event->id : %llu \n", event->id);
	// printk("event->priority : %d \n", event->priority);
	printk("event->hw.config : %llu \n", event->hw.config);
	printk("event->hw.last_tag : %llu \n", event->hw.last_tag);
	printk("event->hw.config_base : %lu \n", event->hw.config_base);
	printk("event->hw.event_base : %lu \n", event->hw.event_base);
	printk("event->hw.idx: %d \n", event->hw.idx);
	printk("event->hw.flags: %d \n", event->hw.flags);
	printk("event->attr.type : %u \n", event->attr.type);
	printk("event->attr.size : %u \n", event->attr.size);
	printk("event->attr.config : %llu \n", event->attr.config);
	printk("event->attr.sample_type : %llu \n", event->attr.sample_type);
	printk("event->attr.read_format : %llu \n", event->attr.read_format);
	printk("-----------------------");
}

/**
  * This is for the final version where we separate perf and tintin
  */

void tintin_print_scheduling_context(struct tintin_event *sc)
{
	printk("[Tintin SC] addr: Ox%p \n", sc);
	printk("[Tintin SC] id: Ox%d \n", sc->context_id);
	printk("[Tintin SC] weight: %d \n", sc->weight);
	printk("[Tintin SC] priority : %llu \n", sc->priority);
	printk("[Tintin SC] uncertainty : %llu \n", sc->uncertainty);
}

/**
 * Init everything in Tintin Scheduling Context
*/

void init_tintin_scheduling_context(struct tintin_event *sc)
{
	sc->context_id = tintin_sc_index;
	sc->updated = 0;
	sc->weight = 1;
	sc->priority = 10;

	//sc->count_variance = 0;
	sc->uncertainty = 0;

	sc->last_scheduled_in_time = 0;
	sc->last_scheduled_out_time = 0;

	sc->total_time_monitored = 0;
	sc->total_time_enabled = 0;
	sc->total_time_unmonitored = 0;
	sc->remaining_time = 0;

	sc->Welfords_total = 0;
	sc->Welfords_total_weight = 0;
	sc->Welfords_mean = 0;
	sc->Welfords_variance_running = 0;
	sc->Welfords_variance = 0;

	sc->running_count = 0;

	// TO REMOVE
	// The memory size should be less than 4Mb
	sc->raw_counts = kzalloc(EVENT_WINDOW_SIZE * sizeof(u64), GFP_KERNEL);
	sc->raw_time_points =
		kzalloc(EVENT_WINDOW_SIZE * sizeof(u64), GFP_KERNEL);
	sc->count_deltas = kzalloc(EVENT_WINDOW_SIZE * sizeof(u64), GFP_KERNEL);
	sc->time_deltas = kzalloc(EVENT_WINDOW_SIZE * sizeof(u64), GFP_KERNEL);
	sc->running_times =
		kzalloc(EVENT_WINDOW_SIZE * sizeof(u64), GFP_KERNEL);
	sc->enable_times = kzalloc(EVENT_WINDOW_SIZE * sizeof(u64), GFP_KERNEL);
	
	//
	if (!sc->raw_counts) {
		printk("Tintin SC kzalloc failed");
	}

	// tintin_sc_index is a global variable
	tintin_sc_list[tintin_sc_index] = sc;
	tintin_sc_index++;
}

/**
 * dealloc a Tintin Scheduling Context
 * This can happen when user request to or when a task exit
*/

void tintin_dealloc_scheduling_context(
	struct tintin_event *to_delete_sc)
{
	kfree(to_delete_sc->raw_counts);
	kfree(to_delete_sc->raw_time_points);
	kfree(to_delete_sc->count_deltas);
	kfree(to_delete_sc->time_deltas);
	kfree(to_delete_sc->running_times);
	kfree(to_delete_sc->enable_times);
}

/**
 * Remove a Tintin Scheduling Context
 * This can happen when user request to or when a task exit
*/

void tintin_remove_scheduling_context(
	struct tintin_event *to_delete_sc)
{
	struct tintin_event *tmp_sc;
	struct tintin_vector* tv_to_remove;
	int num_elements_to_move;
	int i;

	if (tintin_sc_index < 1) {
		// There is not SC in the list
		return;
	}

	// tintin_sc_index is a global variable
	for (i = 0; i < tintin_sc_index; i++) {
		tmp_sc = tintin_sc_list[i];

		if (tmp_sc == to_delete_sc) {
			// we found it

			num_elements_to_move = tintin_sc_index - i;
			// memmove(&tmp_sc, &tmp_sc + sizeof(struct tintin_event*), num_elements_to_move * sizeof(struct tintin_event*));
			tintin_sc_index -= 1;
			tintin_sc_list[i] = tintin_sc_list[tintin_sc_index];

			tv_to_remove = to_delete_sc->count_vec;

			tintin_vector_del(tv_to_remove);
			kfree(tv_to_remove);


			// TO free tmp_sc
			tintin_dealloc_scheduling_context(to_delete_sc);
			kfree(to_delete_sc);

			// printk("[Tintin] One SC is deleted");
			break;
			// or we can use return
		}
	}
}


/*
       _         ___         _
     /`.`.  .-=""_;_""=-.  .','\
    |  \`.`#"'.-" ; "-.`"#'.',  |
     \`=>-Y ,(  O_;_O  ) ,Y=<-'/
      `--'\#>-`-'_;_`-'-<#/`--'
        _  /   .::;::. _ \  _
     .-' `-i-_ |:::::|  `i-' `-._
    '    .-j'  `:::::' `-j-.
       .'  `L'   "T"   `j-. `.
         ,-'|\.___:___./|  `
            | `.____ .' |
            \           /   
             `.       .'
               `.___.'
*/



/*
 * The code below is for the interaction between events and the profiling scopes
 * 
 */

void init_profiling_scope(struct tintin_profiling_context *scope) {
    scope->id = 0;  // Assign some ID
    INIT_LIST_HEAD(&scope->tintin_events);  // Initialize the list
}

void add_scheduling_context_into_scope(struct tintin_profiling_context *scope, struct tintin_event *context) {
    INIT_LIST_HEAD(&context->list);  // Initialize the list head of the context
    list_add_tail(&context->list, &scope->tintin_events);  // Add to the scope's list
}

void print_scheduling_contexts_in_scope(struct tintin_profiling_context *scope) {
    struct tintin_event *context;
    
    list_for_each_entry(context, &scope->tintin_events, list) {
        printk("Scheduling Context ID: %d\n", context->context_id);
    }
}

void remove_scheduling_context_from_scope(struct tintin_event *context) {
    list_del(&context->list);  // Remove the entry from the list
}
