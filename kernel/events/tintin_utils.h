#ifndef TINTIN_UTILS_H
#define TINTIN_UTILS_H

#include <linux/rbtree.h>

// Define a structure that will be stored in the Red-Black tree
struct RBNodeHolder {
    struct rb_node node;
    // Other members of your node structure
};

// Function to count nodes in a Red-Black tree
int count_rb_tree_nodes(struct rb_root *root);

#endif //TINTIN_UTILS_H