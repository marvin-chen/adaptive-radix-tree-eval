#ifndef ART_H
#define ART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) && !defined(__clang__)
# if __STDC_VERSION__ >= 199901L && 402 == (__GNUC__ * 100 + __GNUC_MINOR__)
/*
 * GCC 4.2.2's C99 inline keyword support is pretty broken; avoid. Introduced in
 * GCC 4.2.something, fixed in 4.3.0. So checking for specific major.minor of
 * 4.2 is fine.
 */
#  define BROKEN_GCC_C99_INLINE
# endif
#endif

typedef struct art_node art_node;
typedef struct art_leaf art_leaf;

typedef int(*art_callback)(void *data, const unsigned char *key, uint32_t key_len, void *value);

/*
 * Main struct, points to root.
 */
typedef struct {
    art_node *root;
    uint64_t size;
} art_tree;

/*
 * Structural statistics for explaining node-menu benchmark results.
 */
typedef struct {
    uint64_t keys;
    uint64_t leaf_count;
    uint64_t internal_node_count;

    uint64_t node4_count;
    uint64_t node16_count;
    uint64_t node32_count;
    uint64_t node48_count;
    uint64_t node256_count;

    uint64_t node4_bytes;
    uint64_t node16_bytes;
    uint64_t node32_bytes;
    uint64_t node48_bytes;
    uint64_t node256_bytes;

    uint64_t internal_node_bytes;
    uint64_t leaf_bytes;
    uint64_t key_bytes;
    uint64_t total_bytes;

    uint64_t fanout_hist[257];
    uint64_t leaf_depth_sum;
    uint32_t max_depth;

    uint64_t node64_count;
    uint64_t node64_bytes;
    uint64_t node2_count;
    uint64_t node5_count;
    uint64_t node2_bytes;
    uint64_t node5_bytes;
} art_stats;

/**
 * Initializes an ART tree
 * @return 0 on success.
 */
int art_tree_init(art_tree *t);

/**
 * DEPRECATED
 * Initializes an ART tree
 * @return 0 on success.
 */
#define init_art_tree(...) art_tree_init(__VA_ARGS__)

/**
 * Destroys an ART tree
 * @return 0 on success.
 */
int art_tree_destroy(art_tree *t);

/**
 * DEPRECATED
 * Initializes an ART tree
 * @return 0 on success.
 */
#define destroy_art_tree(...) art_tree_destroy(__VA_ARGS__)

/**
 * Returns the size of the ART tree.
 */
#ifdef BROKEN_GCC_C99_INLINE
# define art_size(t) ((t)->size)
#else
inline uint64_t art_size(art_tree *t) {
    return t->size;
}
#endif

/**
 * inserts a new value into the art tree
 * @arg t the tree
 * @arg key the key
 * @arg key_len the length of the key
 * @arg value opaque value.
 * @return null if the item was newly inserted, otherwise
 * the old value pointer is returned.
 */
void* art_insert(art_tree *t, const unsigned char *key, int key_len, void *value);

/**
 * inserts a new value into the art tree (not replacing)
 * @arg t the tree
 * @arg key the key
 * @arg key_len the length of the key
 * @arg value opaque value.
 * @return null if the item was newly inserted, otherwise
 * the old value pointer is returned.
 */
void* art_insert_no_replace(art_tree *t, const unsigned char *key, int key_len, void *value);

/**
 * Deletes a value from the ART tree
 * @arg t The tree
 * @arg key The key
 * @arg key_len The length of the key
 * @return NULL if the item was not found, otherwise
 * the value pointer is returned.
 */
void* art_delete(art_tree *t, const unsigned char *key, int key_len);

/**
 * Searches for a value in the ART tree
 * @arg t The tree
 * @arg key The key
 * @arg key_len The length of the key
 * @return NULL if the item was not found, otherwise
 * the value pointer is returned.
 */
void* art_search(const art_tree *t, const unsigned char *key, int key_len);

/**
 * Returns the minimum valued leaf
 * @return The minimum leaf or NULL
 */
art_leaf* art_minimum(art_tree *t);

/**
 * Returns the maximum valued leaf
 * @return The maximum leaf or NULL
 */
art_leaf* art_maximum(art_tree *t);

/**
 * Returns the key bytes stored in a leaf.
 */
const unsigned char* art_leaf_key(const art_leaf *l);

/**
 * Returns the length of the key stored in a leaf.
 */
uint32_t art_leaf_key_len(const art_leaf *l);

/**
 * Returns the opaque value stored in a leaf.
 */
void* art_leaf_value(const art_leaf *l);

/**
 * Collects structural statistics for a tree.
 * @arg t The tree
 * @arg stats Output statistics, zeroed and filled by this function
 * @return 0 on success.
 */
int art_collect_stats(const art_tree *t, art_stats *stats);

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
int art_iter(art_tree *t, art_callback cb, void *data);

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
int art_iter_prefix(art_tree *t, const unsigned char *prefix, int prefix_len, art_callback cb, void *data);

#ifdef __cplusplus
}
#endif

#endif
