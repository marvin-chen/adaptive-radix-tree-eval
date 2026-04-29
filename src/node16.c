#include "art_nodes.h"

#include <stdlib.h>
#include <string.h>

#if defined(__i386__) || defined(__amd64__) || defined(__x86_64__)
#include <emmintrin.h>
#define ART_HAS_SSE2 1
#else
#define ART_HAS_SSE2 0
#endif

art_node** art_node16_find_child(art_node16 *n, unsigned char c) {
    int bitfield;
    // support non-x86 architectures.
#if ART_HAS_SSE2
    // Compare the key to all 16 stored keys.
    __m128i cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c), _mm_loadu_si128((__m128i*)n->keys));
    bitfield = _mm_movemask_epi8(cmp);
#else
    // Compare the key to all 16 stored keys.
    bitfield = 0;
    for (int i = 0; i < 16; ++i) {
        if (n->keys[i] == c) bitfield |= (1 << i);
    }
#endif

    // Use a mask to ignore children that don't exist.
    bitfield &= (1 << n->n.num_children) - 1;

    /*
     * If we have a match (any bit set) then we can
     * return the pointer match using ctz to get
     * the index.
     */
    return bitfield ? &n->children[__builtin_ctz(bitfield)] : NULL;
}

void art_node16_add_child(art_node16 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 16) {
        unsigned mask = (1 << n->n.num_children) - 1;
        unsigned bitfield;
        // support non-x86 architectures.
#if ART_HAS_SSE2
        // Compare the key to all 16 stored keys.
        __m128i cmp = _mm_cmplt_epi8(_mm_set1_epi8(c), _mm_loadu_si128((__m128i*)n->keys));
        bitfield = _mm_movemask_epi8(cmp) & mask;
#else
        // Compare the key to all 16 stored keys.
        bitfield = 0;
        for (short i = 0; i < 16; ++i) {
            if (c < n->keys[i]) bitfield |= (1 << i);
        }

        // Use a mask to ignore children that don't exist.
        bitfield &= mask;
#endif

        // Check if less than any.
        unsigned idx = bitfield ? __builtin_ctz(bitfield) : n->n.num_children;
        if (bitfield) {
            memmove(n->keys + idx + 1, n->keys + idx, n->n.num_children - idx);
            memmove(n->children + idx + 1, n->children + idx,
                (n->n.num_children - idx) * sizeof(void*));
        }

        // Set the child.
        n->keys[idx] = c;
        n->children[idx] = (art_node*)child;
        n->n.num_children++;
        return;
    }

    art_node48 *new_node = (art_node48*)art_alloc_node(NODE48);
    // Copy the child pointers and populate the key map.
    memcpy(new_node->children, n->children, sizeof(void*) * n->n.num_children);
    for (int i = 0; i < n->n.num_children; i++) {
        new_node->keys[n->keys[i]] = i + 1;
    }
    art_copy_header((art_node*)new_node, (art_node*)n);
    *ref = (art_node*)new_node;
    free(n);
    art_add_child((art_node*)new_node, ref, c, child);
}

void art_node16_remove_child(art_node16 *n, art_node **ref, art_node **slot) {
    int pos = slot - n->children;
    memmove(n->keys + pos, n->keys + pos + 1, n->n.num_children - 1 - pos);
    memmove(n->children + pos, n->children + pos + 1,
            (n->n.num_children - 1 - pos) * sizeof(void*));
    n->n.num_children--;

    if (n->n.num_children == 3) {
        art_node4 *new_node = (art_node4*)art_alloc_node(NODE4);
        *ref = (art_node*)new_node;
        art_copy_header((art_node*)new_node, (art_node*)n);
        memcpy(new_node->keys, n->keys, 4);
        memcpy(new_node->children, n->children, 4 * sizeof(void*));
        free(n);
    }
}

art_node* art_node16_first_child(const art_node16 *n) {
    return n->children[0];
}

art_node* art_node16_last_child(const art_node16 *n) {
    return n->children[n->n.num_children - 1];
}

int art_node16_for_each_child(art_node16 *n, art_child_cb cb, void *data) {
    for (int i = 0; i < n->n.num_children; i++) {
        int res = cb(n->children[i], data);
        if (res) return res;
    }
    return 0;
}
