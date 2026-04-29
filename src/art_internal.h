#ifndef ART_INTERNAL_H
#define ART_INTERNAL_H

#include "art.h"

#include <stdint.h>
#include <string.h>

#define NODE4   1
#define NODE16  2
#define NODE48  3
#define NODE256 4

#define MAX_PREFIX_LEN 10

/*
 * Macros to manipulate pointer tags.
 */
#define IS_LEAF(x) (((uintptr_t)(x) & 1))
#define SET_LEAF(x) ((void*)((uintptr_t)(x) | 1))
#define LEAF_RAW(x) ((art_leaf*)((void*)((uintptr_t)(x) & ~1)))

/*
 * This struct is included as part
 * of all the various node sizes.
 */
struct art_node {
    uint32_t partial_len;
    uint8_t type;
    uint8_t num_children;
    unsigned char partial[MAX_PREFIX_LEN];
};

/*
 * Small node with only 4 children.
 */
typedef struct {
    art_node n;
    unsigned char keys[4];
    art_node *children[4];
} art_node4;

/*
 * Node with 16 children.
 */
typedef struct {
    art_node n;
    unsigned char keys[16];
    art_node *children[16];
} art_node16;

/*
 * Node with 48 children, but
 * a full 256 byte field.
 */
typedef struct {
    art_node n;
    unsigned char keys[256];
    art_node *children[48];
} art_node48;

/*
 * Full node with 256 children.
 */
typedef struct {
    art_node n;
    art_node *children[256];
} art_node256;

/*
 * Represents a leaf. These are
 * of arbitrary size, as they include the key.
 */
struct art_leaf {
    void *value;
    uint32_t key_len;
    unsigned char key[];
};

// Simple inlined if.
static inline int art_min_int(int a, int b) {
    return (a < b) ? a : b;
}

static inline void art_copy_header(art_node *dest, const art_node *src) {
    dest->num_children = src->num_children;
    dest->partial_len = src->partial_len;
    memcpy(dest->partial, src->partial, art_min_int(MAX_PREFIX_LEN, src->partial_len));
}

#endif
