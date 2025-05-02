#include "tintin_profiling_context.h"
#include <linux/rbtree.h>

#define MAX_NUM_TINTIN_SCOPE 0x414414 // magic number 414414 ~ tintin

struct profiling_context_tree* tintin_tree;
EXPORT_SYMBOL(tintin_tree);

spinlock_t tintin_tree_lock;

/************************************************************
 * Tree related functions 
 *
 ************************************************************/

struct profiling_context_tree *get_tintin_tree(void)
{
    struct profiling_context_tree *tree;

    /* Check if the tree already exists (fast path) */
    tree = READ_ONCE(tintin_tree);
    if (likely(tree))
        return tree;

    /* Slow path: initialize the tree */
    spin_lock(&tintin_tree_lock);
    
    /* Check again in case another thread created it while we were waiting */
    if (!tintin_tree) {
        tree = kzalloc(sizeof(*tree), GFP_KERNEL);
        if (!tree) {
            spin_unlock(&tintin_tree_lock);
            return NULL;
        }

        tree->root = RB_ROOT;
        tree->rb_leftmost = NULL;
        tree->num_contexts = 0;
        raw_spin_lock_init(&tree->lock);

        /* Publish the tree */
        smp_store_release(&tintin_tree, tree);
    } else {
        tree = tintin_tree;
    }
    
    spin_unlock(&tintin_tree_lock);
    return tree;
}
EXPORT_SYMBOL(get_tintin_tree);

void init_profiling_tree(struct profiling_context_tree *tree)
{
	tree->num_contexts = 0;
	tree->rb_leftmost = NULL;
	raw_spin_lock_init(&tree->lock);
    RB_CLEAR_NODE(tree->root.rb_node);
}

int add_context_into_tree(struct profiling_context_tree *tree,
			struct tintin_profiling_context *scope)
{
	struct rb_node **p = &tree->root.rb_node;
	struct rb_node *parent = NULL;
	struct tintin_profiling_context *this_scope;

	// Assign the scope_id upon insertion
	int scope_id = tree->num_contexts;

	raw_spin_lock(&tree->lock);
	while (*p) {
		parent = *p;
		this_scope = rb_entry(parent, struct tintin_profiling_context,
				      rb_node);
		if (scope_id < this_scope->id) {
			p = &(*p)->rb_left;
		} else if (scope_id > this_scope->id) {
			p = &(*p)->rb_right;
		} else {
			raw_spin_unlock(&tree->lock);
			return -1;
		}
	}

	rb_link_node(&scope->rb_node, parent, p);
	rb_insert_color(&scope->rb_node, &tree->root);

	// Inserted successfully
	tree->num_contexts++;
	raw_spin_unlock(&tree->lock);
	return 0;
}

int remove_context_from_tree(struct profiling_context_tree *tree,
			   struct tintin_profiling_context *scope)
{
	raw_spin_lock(&tree->lock);
	rb_erase(&scope->rb_node, &tree->root);
	tree->num_contexts--;
	raw_spin_unlock(&tree->lock);
	return 0;
}

/************************************************************
 * Context related functions
 *
 ************************************************************/

int tintin_init_profiling_context(struct tintin_profiling_context *epx)
{
	struct profiling_context_tree *tree = get_tintin_tree();
	if (!tree)
		return -ENOMEM;

	epx->id = tree->num_contexts;
	epx->num_events = 0;
	INIT_LIST_HEAD(&epx->tintin_events);

	if (add_context_into_tree(tree, epx)) {
		return -EINVAL;
	}
	return 0;
}

int tintin_free_profiling_context(struct tintin_profiling_context *epx)
{
	struct tintin_event *tevent;
	struct list_head *pos, *q;

	raw_spin_lock(&epx->lock);
	list_for_each_safe (pos, q, &epx->tintin_events) {
		tevent = list_entry(pos, struct tintin_event,
				list);
		tintin_dealloc_scheduling_context(tevent);
	}
	raw_spin_unlock(&epx->lock);
	return 0;
}

int tintin_add_event_into_contexxt(
	struct tintin_profiling_context *context,
	struct tintin_event *tevent)
{
	INIT_LIST_HEAD(&tevent->list);

	raw_spin_lock(&context->lock);
	list_add_tail(&tevent->list, &context->tintin_events);
	raw_spin_unlock(&context->lock);

	context->num_events++;
	return 0;
}

int tintin_remove_event_from_context(
	struct tintin_profiling_context *context,
	struct tintin_event *tevent)
{
	raw_spin_lock(&context->lock);
	list_del(&tevent->list);
	context->num_events--;
	raw_spin_unlock(&context->lock);
	return 0;
}

inline int active_context(struct tintin_profiling_context *context)
{
	context->is_active = true;
	return 0;
}

inline int dective_context(struct tintin_profiling_context *context)
{
	context->is_active = false;
	return 0;
}