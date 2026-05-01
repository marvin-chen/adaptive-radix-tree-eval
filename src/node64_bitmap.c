#include "art_nodes.h"

#include <stdlib.h>
#include <string.h>

#if ART_HAS_NODE64 && ART_NODE64_BITMAP

static uint64_t node64_bit(unsigned char c) {
    return UINT64_C(1) << (c & 63);
}

static unsigned node64_rank(const art_node64 *n, unsigned char c) {
    unsigned word = c >> 6;
    unsigned bit = c & 63;
    unsigned rank = 0;

    for (unsigned i = 0; i < word; i++) {
        rank += (unsigned)__builtin_popcountll(n->bitmap[i]);
    }
    if (bit) {
        rank += (unsigned)__builtin_popcountll(n->bitmap[word] & ((UINT64_C(1) << bit) - 1));
    }
    return rank;
}

art_node** art_node64_find_child(art_node64 *n, unsigned char c) {
    unsigned word = c >> 6;
    uint64_t bit = node64_bit(c);
    if (!(n->bitmap[word] & bit)) return NULL;
    return &n->children[node64_rank(n, c)];
}

void art_node64_add_child(art_node64 *n, unsigned char c, void *child) {
    if (n->n.num_children >= 64) abort();

    unsigned word = c >> 6;
    uint64_t bit = node64_bit(c);
    if (n->bitmap[word] & bit) abort();

    unsigned idx = node64_rank(n, c);
    memmove(n->children + idx + 1, n->children + idx,
            (n->n.num_children - idx) * sizeof(void*));
    n->children[idx] = (art_node*)child;
    n->bitmap[word] |= bit;
    n->n.num_children++;
}

void art_node64_remove_child(art_node64 *n, unsigned char c) {
    unsigned word = c >> 6;
    uint64_t bit = node64_bit(c);
    if (!(n->bitmap[word] & bit)) abort();

    unsigned idx = node64_rank(n, c);
    n->bitmap[word] &= ~bit;
    memmove(n->children + idx, n->children + idx + 1,
            (n->n.num_children - 1 - idx) * sizeof(void*));
    n->children[n->n.num_children - 1] = NULL;
    n->n.num_children--;
}

art_node* art_node64_first_child(const art_node64 *n) {
    return n->children[0];
}

art_node* art_node64_last_child(const art_node64 *n) {
    return n->children[n->n.num_children - 1];
}

int art_node64_for_each_child(art_node64 *n, art_child_cb cb, void *data) {
    for (int i = 0; i < n->n.num_children; i++) {
        int res = cb(n->children[i], data);
        if (res) return res;
    }
    return 0;
}

#endif
