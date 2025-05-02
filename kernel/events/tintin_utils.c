#include "tintin_utils.h"

int count_rb_tree_nodes(struct rb_root *root) {
    int count = 0;
    struct rb_node *node;

    for (node = rb_first(root); node; node = rb_next(node)) {
        // Assuming each node in the tree is of type struct RBNodeHolder
        struct RBNodeHolder *entry = rb_entry(node, struct RBNodeHolder, node);
        // Increment the counter for each node encountered
        count++;
    }

    return count;
}