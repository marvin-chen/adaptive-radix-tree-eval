#include "art_nodes.h"

#include <stdlib.h>
#include <string.h>

#if defined(__i386__) || defined(__amd64__) || defined(__x86_64__)
#include <emmintrin.h>
#define ART_HAS_SSE2 1
#else
#define ART_HAS_SSE2 0
#endif

#if ART_HAS_NODE32

art_node** art_node32_find_child(art_node32 *n, unsigned char c) {
#if ART_HAS_SSE2
    __m128i needle = _mm_set1_epi8(c);
    __m128i cmp = _mm_cmpeq_epi8(needle, _mm_loadu_si128((__m128i*)n->keys));
    uint32_t bitfield = (uint32_t)_mm_movemask_epi8(cmp);

    if (n->n.num_children > 16) {
        cmp = _mm_cmpeq_epi8(needle, _mm_loadu_si128((__m128i*)(n->keys + 16)));
        bitfield |= (uint32_t)_mm_movemask_epi8(cmp) << 16;
    }
#else
    uint32_t bitfield = 0;
    for (int i = 0; i < n->n.num_children; ++i) {
        if (n->keys[i] == c) bitfield |= (uint32_t)1 << i;
    }
#endif
    uint32_t mask = n->n.num_children == 32
        ? UINT32_MAX
        : (((uint32_t)1 << n->n.num_children) - 1u);
    bitfield &= mask;
    return bitfield ? &n->children[__builtin_ctz(bitfield)] : NULL;
}

void art_node32_add_child(art_node32 *n, unsigned char c, void *child) {
    if (n->n.num_children >= 32) abort();

    int idx;
    for (idx = 0; idx < n->n.num_children; idx++) {
        if (c < n->keys[idx]) break;
    }

    // Shift to make room.
    memmove(n->keys + idx + 1, n->keys + idx, n->n.num_children - idx);
    memmove(n->children + idx + 1, n->children + idx,
            (n->n.num_children - idx) * sizeof(void*));

    // Insert element.
    n->keys[idx] = c;
    n->children[idx] = (art_node*)child;
    n->n.num_children++;
}

void art_node32_remove_child(art_node32 *n, art_node **slot) {
    int pos = slot - n->children;
    memmove(n->keys + pos, n->keys + pos + 1, n->n.num_children - 1 - pos);
    memmove(n->children + pos, n->children + pos + 1,
            (n->n.num_children - 1 - pos) * sizeof(void*));
    n->n.num_children--;
}

art_node* art_node32_first_child(const art_node32 *n) {
    return n->children[0];
}

art_node* art_node32_last_child(const art_node32 *n) {
    return n->children[n->n.num_children - 1];
}

int art_node32_for_each_child(art_node32 *n, art_child_cb cb, void *data) {
    for (int i = 0; i < n->n.num_children; i++) {
        int res = cb(n->children[i], data);
        if (res) return res;
    }
    return 0;
}

#endif
