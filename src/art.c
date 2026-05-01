#include "art_internal.h"
#include "art_nodes.h"

#include <stdlib.h>
#include <string.h>

/**
 * Returns the size of the ART tree.
 */
#ifndef BROKEN_GCC_C99_INLINE
extern inline uint64_t art_size(art_tree *t);
#endif

/**
 * Initializes an ART tree
 * @return 0 on success.
 */
int art_tree_init(art_tree *t) {
    t->root = NULL;
    t->size = 0;
    return 0;
}

// Recursively destroys the tree.
static void destroy_node(art_node *n);

static int destroy_child(art_node *child, void *data) {
    (void)data;
    destroy_node(child);
    return 0;
}

static void destroy_node(art_node *n) {
    // Break if null.
    if (!n) {
        return;
    }

    // Special case leafs.
    if (IS_LEAF(n)) {
        free(LEAF_RAW(n));
        return;
    }

    art_for_each_child(n, destroy_child, NULL);

    // Free ourself on the way up.
    free(n);
}

/**
 * Destroys an ART tree
 * @return 0 on success.
 */
int art_tree_destroy(art_tree *t) {
    destroy_node(t->root);
    return 0;
}

const unsigned char *art_leaf_key(const art_leaf *l) {
    return l->key;
}

uint32_t art_leaf_key_len(const art_leaf *l) {
    return l->key_len;
}

void *art_leaf_value(const art_leaf *l) {
    return l->value;
}

static uint64_t record_node_type(art_stats *stats, const art_node *n) {
    switch (n->type) {
#if ART_HAS_NODE2
        case NODE2:
            stats->node2_count++;
            stats->node2_bytes += sizeof(art_node2);
            return sizeof(art_node2);
#endif
        case NODE4:
            stats->node4_count++;
            stats->node4_bytes += sizeof(art_node4);
            return sizeof(art_node4);
#if ART_HAS_NODE5
        case NODE5:
            stats->node5_count++;
            stats->node5_bytes += sizeof(art_node5);
            return sizeof(art_node5);
#endif
        case NODE16:
            stats->node16_count++;
            stats->node16_bytes += sizeof(art_node16);
            return sizeof(art_node16);
#if ART_HAS_NODE32
        case NODE32:
            stats->node32_count++;
            stats->node32_bytes += sizeof(art_node32);
            return sizeof(art_node32);
#endif
        case NODE48:
            stats->node48_count++;
            stats->node48_bytes += sizeof(art_node48);
            return sizeof(art_node48);
#if ART_HAS_NODE64
        case NODE64:
            stats->node64_count++;
            stats->node64_bytes += sizeof(art_node64);
            return sizeof(art_node64);
#endif
        case NODE256:
            stats->node256_count++;
            stats->node256_bytes += sizeof(art_node256);
            return sizeof(art_node256);
        default:
            return 0;
    }
}

static void collect_node_stats(art_node *n, art_stats *stats, uint32_t depth);

struct stats_child_ctx {
    art_stats *stats;
    uint32_t depth;
    uint32_t child_count;
};

static int collect_child_stats(art_node *child, void *data) {
    struct stats_child_ctx *ctx = data;
    ctx->child_count++;
    collect_node_stats(child, ctx->stats, ctx->depth);
    return 0;
}

static void collect_node_stats(art_node *n, art_stats *stats, uint32_t depth) {
    if (!n) {
        return;
    }

    if (IS_LEAF(n)) {
        art_leaf *l = LEAF_RAW(n);
        stats->leaf_count++;
        stats->leaf_bytes += sizeof(art_leaf) + l->key_len;
        stats->key_bytes += l->key_len;
        stats->leaf_depth_sum += depth;
        if (depth > stats->max_depth) {
            stats->max_depth = depth;
        }
        return;
    }

    stats->internal_node_count++;
    stats->internal_node_bytes += record_node_type(stats, n);

    struct stats_child_ctx ctx = {
        stats,
        depth + n->partial_len + 1,
        0
    };
    art_for_each_child(n, collect_child_stats, &ctx);
    if (ctx.child_count <= 256) {
        stats->fanout_hist[ctx.child_count]++;
    }
}

int art_collect_stats(const art_tree *t, art_stats *stats) {
    memset(stats, 0, sizeof(*stats));
    stats->keys = t->size;
    collect_node_stats(t->root, stats, 0);
    stats->total_bytes = stats->internal_node_bytes + stats->leaf_bytes;
    return 0;
}

/**
 * Returns the number of prefix characters shared between
 * the key and node.
 */
static int check_prefix(const art_node *n, const unsigned char *key, int key_len, int depth) {
    int max_cmp = art_min_int(art_min_int(n->partial_len, MAX_PREFIX_LEN), key_len - depth);
    int idx;

    for (idx = 0; idx < max_cmp; idx++) {
        if (n->partial[idx] != key[depth + idx]) {
            return idx;
        }
    }
    return idx;
}

/**
 * Checks if a leaf matches
 * @return 0 on success.
 */
static int leaf_matches(const art_leaf *n, const unsigned char *key, int key_len, int depth) {
    (void)depth;

    // Fail if the key lengths are different.
    if (n->key_len != (uint32_t)key_len) {
        return 1;
    }

    // Compare the keys starting at the depth.
    return memcmp(n->key, key, key_len);
}

static art_leaf *make_leaf(const unsigned char *key, int key_len, void *value) {
    art_leaf *l = (art_leaf *)calloc(1, sizeof(art_leaf) + key_len);
    l->value = value;
    l->key_len = key_len;
    memcpy(l->key, key, key_len);
    return l;
}

static int longest_common_prefix(const art_leaf *l1, const art_leaf *l2, int depth) {
    int max_cmp = art_min_int(l1->key_len, l2->key_len) - depth;
    int idx;

    for (idx = 0; idx < max_cmp; idx++) {
        if (l1->key[depth + idx] != l2->key[depth + idx]) {
            return idx;
        }
    }
    return idx;
}

// Find the minimum leaf under a node.
static art_leaf *minimum(const art_node *n) {
    // Handle base cases.
    if (!n) {
        return NULL;
    }
    if (IS_LEAF(n)) {
        return LEAF_RAW(n);
    }
    if (n->num_children == 0) {
        return NULL;
    }
    return minimum(art_first_child(n));
}

// Find the maximum leaf under a node.
static art_leaf *maximum(const art_node *n) {
    // Handle base cases.
    if (!n) {
        return NULL;
    }
    if (IS_LEAF(n)) {
        return LEAF_RAW(n);
    }
    if (n->num_children == 0) {
        return NULL;
    }
    return maximum(art_last_child(n));
}

/**
 * Returns the minimum valued leaf
 */
art_leaf *art_minimum(art_tree *t) {
    return minimum(t->root);
}

/**
 * Returns the maximum valued leaf
 */
art_leaf *art_maximum(art_tree *t) {
    return maximum(t->root);
}

/**
 * Calculates the index at which the prefixes mismatch.
 */
static int prefix_mismatch(const art_node *n, const unsigned char *key, int key_len, int depth) {
    int max_cmp = art_min_int(art_min_int(MAX_PREFIX_LEN, n->partial_len), key_len - depth);
    int idx;

    for (idx = 0; idx < max_cmp; idx++) {
        if (n->partial[idx] != key[depth + idx]) {
            return idx;
        }
    }

    // If the prefix is short we can avoid finding a leaf.
    if (n->partial_len > MAX_PREFIX_LEN) {
        // Prefix is longer than what we've checked, find a leaf.
        art_leaf *l = minimum(n);
        max_cmp = art_min_int(l->key_len, key_len) - depth;
        for (; idx < max_cmp; idx++) {
            if (l->key[idx + depth] != key[depth + idx]) {
                return idx;
            }
        }
    }
    return idx;
}

/**
 * Searches for a value in the ART tree
 * @arg t The tree
 * @arg key The key
 * @arg key_len The length of the key
 * @return NULL if the item was not found, otherwise
 * the value pointer is returned.
 */
void *art_search(const art_tree *t, const unsigned char *key, int key_len) {
    art_node **child;
    art_node *n = t->root;
    int depth = 0;

    while (n) {
        // Might be a leaf.
        if (IS_LEAF(n)) {
            art_leaf *l = LEAF_RAW(n);

            // Check if the expanded path matches.
            return leaf_matches(l, key, key_len, depth) ? NULL : l->value;
        }

        // Bail if the prefix does not match.
        if (n->partial_len) {
            int prefix_len = check_prefix(n, key, key_len, depth);
            if (prefix_len != art_min_int(MAX_PREFIX_LEN, n->partial_len)) {
                return NULL;
            }
            depth = depth + n->partial_len;
        }

        // Recursively search.
        child = art_find_child(n, key[depth]);
        n = child ? *child : NULL;
        depth++;
    }
    return NULL;
}

static void *recursive_insert(art_node *n, art_node **ref, const unsigned char *key,
        int key_len, void *value, int depth, int *old, int replace) {
    // If we are at a NULL node, inject a leaf.
    if (!n) {
        *ref = SET_LEAF(make_leaf(key, key_len, value));
        return NULL;
    }

    // If we are at a leaf, we need to replace it with a node.
    if (IS_LEAF(n)) {
        art_leaf *l = LEAF_RAW(n);

        // Check if we are updating an existing value.
        if (!leaf_matches(l, key, key_len, depth)) {
            *old = 1;
            void *old_val = l->value;
            if (replace) {
                l->value = value;
            }
            return old_val;
        }

        // New value, split the leaf into the menu's smallest internal node.
        art_node *new_node = art_alloc_node(art_menu_min_type());

        // Create a new leaf.
        art_leaf *l2 = make_leaf(key, key_len, value);

        // Determine longest prefix.
        int longest_prefix = longest_common_prefix(l, l2, depth);
        new_node->partial_len = longest_prefix;
        memcpy(new_node->partial, key + depth, art_min_int(MAX_PREFIX_LEN, longest_prefix));

        // Add the leafs to the new node.
        *ref = new_node;

        art_add_child(new_node, ref, l->key[depth + longest_prefix], SET_LEAF(l));
        art_add_child(new_node, ref, l2->key[depth + longest_prefix], SET_LEAF(l2));
        return NULL;
    }

    // Check if given node has a prefix.
    if (n->partial_len) {
        // Determine if the prefixes differ, since we need to split.
        int prefix_diff = prefix_mismatch(n, key, key_len, depth);
        if ((uint32_t)prefix_diff >= n->partial_len) {
            depth += n->partial_len;
            goto RECURSE_SEARCH;
        }

        // Create a new node.
        art_node *new_node = art_alloc_node(art_menu_min_type());
        *ref = new_node;
        new_node->partial_len = prefix_diff;
        memcpy(new_node->partial, n->partial, art_min_int(MAX_PREFIX_LEN, prefix_diff));

        // Adjust the prefix of the old node.
        if (n->partial_len <= MAX_PREFIX_LEN) {
            art_add_child(new_node, ref, n->partial[prefix_diff], n);
            n->partial_len -= (prefix_diff + 1);
            memmove(n->partial, n->partial + prefix_diff + 1,
                    art_min_int(MAX_PREFIX_LEN, n->partial_len));
        } else {
            n->partial_len -= (prefix_diff + 1);
            art_leaf *l = minimum(n);
            art_add_child(new_node, ref, l->key[depth + prefix_diff], n);
            memcpy(n->partial, l->key + depth + prefix_diff + 1,
                    art_min_int(MAX_PREFIX_LEN, n->partial_len));
        }

        // Insert the new leaf.
        art_leaf *l = make_leaf(key, key_len, value);
        art_add_child(new_node, ref, key[depth + prefix_diff], SET_LEAF(l));
        return NULL;
    }

RECURSE_SEARCH:;

    // Find a child to recurse to.
    art_node **child = art_find_child(n, key[depth]);
    if (child) {
        return recursive_insert(*child, child, key, key_len, value, depth + 1, old, replace);
    }

    // No child, node goes within us.
    art_leaf *l = make_leaf(key, key_len, value);
    art_add_child(n, ref, key[depth], SET_LEAF(l));
    return NULL;
}

/**
 * inserts a new value into the art tree
 * @arg t the tree
 * @arg key the key
 * @arg key_len the length of the key
 * @arg value opaque value.
 * @return null if the item was newly inserted, otherwise
 * the old value pointer is returned.
 */
void *art_insert(art_tree *t, const unsigned char *key, int key_len, void *value) {
    int old = 0;
    void *old_val = recursive_insert(t->root, &t->root, key, key_len, value, 0, &old, 1);
    if (!old) {
        t->size++;
    }
    return old_val;
}

/**
 * inserts a new value into the art tree (no replace)
 * @arg t the tree
 * @arg key the key
 * @arg key_len the length of the key
 * @arg value opaque value.
 * @return null if the item was newly inserted, otherwise
 * the old value pointer is returned.
 */
void *art_insert_no_replace(art_tree *t, const unsigned char *key, int key_len, void *value) {
    int old = 0;
    void *old_val = recursive_insert(t->root, &t->root, key, key_len, value, 0, &old, 0);
    if (!old) {
        t->size++;
    }
    return old_val;
}

static int recursive_delete(art_node *n, art_node **ref, const unsigned char *key,
        int key_len, int depth, void **value) {
    // Search terminated.
    if (!n) {
        return 0;
    }

    // Handle hitting a leaf node.
    if (IS_LEAF(n)) {
        art_leaf *l = LEAF_RAW(n);
        if (leaf_matches(l, key, key_len, depth)) {
            return 0;
        }

        *value = l->value;
        *ref = NULL;
        free(l);
        return 1;
    }

    // Bail if the prefix does not match.
    if (n->partial_len) {
        int prefix_len = check_prefix(n, key, key_len, depth);
        if (prefix_len != art_min_int(MAX_PREFIX_LEN, n->partial_len)) {
            return 0;
        }
        depth = depth + n->partial_len;
    }

    // Find child node.
    art_node **child = art_find_child(n, key[depth]);
    if (!child) {
        return 0;
    }

    // If the child is leaf, delete from this node.
    if (IS_LEAF(*child)) {
        art_leaf *l = LEAF_RAW(*child);
        if (leaf_matches(l, key, key_len, depth)) {
            return 0;
        }

        *value = l->value;
        art_remove_child(n, ref, key[depth], child);
        free(l);
        return 1;
    }

    // Recurse.
    return recursive_delete(*child, child, key, key_len, depth + 1, value);
}

/**
 * Deletes a value from the ART tree
 * @arg t The tree
 * @arg key The key
 * @arg key_len The length of the key
 * @return NULL if the item was not found, otherwise
 * the value pointer is returned.
 */
void *art_delete(art_tree *t, const unsigned char *key, int key_len) {
    void *value = NULL;
    int deleted = recursive_delete(t->root, &t->root, key, key_len, 0, &value);
    if (deleted) {
        t->size--;
    }
    return value;
}

// Recursively iterates over the tree.
static int recursive_iter(art_node *n, art_callback cb, void *data);

struct iter_ctx {
    art_callback cb;
    void *data;
};

static int iter_child(art_node *child, void *data) {
    struct iter_ctx *ctx = data;
    return recursive_iter(child, ctx->cb, ctx->data);
}

static int recursive_iter(art_node *n, art_callback cb, void *data) {
    // Handle base cases.
    if (!n) {
        return 0;
    }
    if (IS_LEAF(n)) {
        art_leaf *l = LEAF_RAW(n);
        return cb(data, (const unsigned char *)l->key, l->key_len, l->value);
    }

    struct iter_ctx ctx = { cb, data };
    return art_for_each_child(n, iter_child, &ctx);
}

/**
 * Iterates through the entries pairs in the map,
 * invoking a callback for each. The call back gets a
 * key, value for each and returns an integer stop value.
 * If the callback returns non-zero, then the iteration stops.
 * @arg t The tree to iterate over
 * @arg cb The callback function to invoke
 * @arg data Opaque handle passed to the callback
 * @return 0 on success, or the return of the callback.
 */
int art_iter(art_tree *t, art_callback cb, void *data) {
    return recursive_iter(t->root, cb, data);
}

/**
 * Checks if a leaf prefix matches
 * @return 0 on success.
 */
static int leaf_prefix_matches(const art_leaf *n, const unsigned char *prefix, int prefix_len) {
    if (!n) {
        return 1;
    }

    // Fail if the key length is too short.
    if (n->key_len < (uint32_t)prefix_len) {
        return 1;
    }

    // Compare the keys.
    return memcmp(n->key, prefix, prefix_len);
}

/**
 * Iterates through the entries pairs in the map,
 * invoking a callback for each that matches a given prefix.
 * The call back gets a key, value for each and returns an integer stop value.
 * If the callback returns non-zero, then the iteration stops.
 * @arg t The tree to iterate over
 * @arg prefix The prefix of keys to read
 * @arg prefix_len The length of the prefix
 * @arg cb The callback function to invoke
 * @arg data Opaque handle passed to the callback
 * @return 0 on success, or the return of the callback.
 */
int art_iter_prefix(art_tree *t, const unsigned char *key, int key_len, art_callback cb, void *data) {
    art_node **child;
    art_node *n = t->root;
    int prefix_len;
    int depth = 0;

    if (key_len <= 0) {
        return recursive_iter(n, cb, data);
    }

    while (n) {
        // Might be a leaf.
        if (IS_LEAF(n)) {
            art_leaf *l = LEAF_RAW(n);

            // Check if the expanded path matches.
            if (!leaf_prefix_matches(l, key, key_len)) {
                return cb(data, (const unsigned char *)l->key, l->key_len, l->value);
            }
            return 0;
        }

        // If the prefix has been consumed, we need to handle this node.
        if (depth >= key_len) {
            return recursive_iter(n, cb, data);
        }

        // Bail if the prefix does not match.
        if (n->partial_len) {
            prefix_len = prefix_mismatch(n, key, key_len, depth);

            // Guard if the mis-match is longer than the MAX_PREFIX_LEN.
            if ((uint32_t)prefix_len > n->partial_len) {
                prefix_len = n->partial_len;
            }

            // If there is no match, search is terminated.
            if (!prefix_len) {
                return 0;

            // If we've matched the prefix, iterate on this node.
            } else if (depth + prefix_len >= key_len) {
                return recursive_iter(n, cb, data);
            }

            // If there is a full match, go deeper.
            depth = depth + n->partial_len;
        }

        // Recursively search.
        if (depth >= key_len) {
            return recursive_iter(n, cb, data);
        }
        child = art_find_child(n, key[depth]);
        n = child ? *child : NULL;
        depth++;
    }
    return 0;
}
