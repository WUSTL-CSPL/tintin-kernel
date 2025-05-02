#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tintin_event.h>
#include "tintin_scheduler.h"
#include "tintin_utils.h"
#include "tintin_estimation.h"
#include "tintin_uncertainty.h"
#include <linux/tintin_vector.h>


enum profiling_scope {
    SCOPE_TASK = 0,
    SCOPE_CPU,
    SCOPE_CODE_REGION,
    SCOPE_ALL
};

struct tintin_profiling_context {
    int id;
    bool is_active;    // Flag to indicate if the scope is active
    enum profiling_scope scope_type;    // Type of the scope
    struct list_head tintin_events;  // List of scheduling contexts
    unsigned num_events;  // Number of events in the scope
    struct rb_node rb_node;    // Pointer to the node in the Context Tree

    raw_spinlock_t lock;    // Lock for the scope
};

struct profiling_context_tree {
    struct rb_root root;  // Red-black tree of tasks
    struct rb_node *rb_leftmost;    // Pointer to the leftmost node
    unsigned num_contexts;       // Number of running tasks

    raw_spinlock_t lock;    // Lock for the tree
};


// Tree-related functions
struct profiling_context_tree *get_tintin_tree(void);
void init_profiling_tree(struct profiling_context_tree *tree);
int add_context_into_tree(struct profiling_context_tree *tree, struct tintin_profiling_context *scope);
int remove_context_from_tree(struct profiling_context_tree *tree, struct tintin_profiling_context *scope);

// Context-related functions
int tintin_init_profiling_context(struct tintin_profiling_context *epx);
int tintin_free_profiling_context(struct tintin_profiling_context *epx);
int tintin_add_event_into_context(struct tintin_profiling_context *context, struct tintin_event *tevent);
int tintin_remove_event_from_context(struct tintin_profiling_context *context, struct tintin_event *tevent);

inline int active_context(struct tintin_profiling_context *context);
inline int dective_context(struct tintin_profiling_context *context);