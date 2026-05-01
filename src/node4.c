#include "art_nodes.h"

#include <stdlib.h>
#include <string.h>

art_node** art_node4_find_child(art_node4 *n, unsigned char c) {
    for (int i = 0; i < n->n.num_children; i++) {
        /*
         * This cast works around a bug in gcc 5.1 when unrolling loops:
         * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59124
         */
        if (((unsigned char*)n->keys)[i] == c) return &n->children[i];
    }
    return NULL;
}

void art_node4_add_child(art_node4 *n, unsigned char c, void *child) {
    if (n->n.num_children >= 4) abort();

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

void art_node4_remove_child(art_node4 *n, art_node **slot) {
    int pos = slot - n->children;
    memmove(n->keys + pos, n->keys + pos + 1, n->n.num_children - 1 - pos);
    memmove(n->children + pos, n->children + pos + 1,
            (n->n.num_children - 1 - pos) * sizeof(void*));
    n->n.num_children--;
}

art_node* art_node4_first_child(const art_node4 *n) {
    return n->children[0];
}

art_node* art_node4_last_child(const art_node4 *n) {
    return n->children[n->n.num_children - 1];
}

int art_node4_for_each_child(art_node4 *n, art_child_cb cb, void *data) {
    for (int i = 0; i < n->n.num_children; i++) {
        int res = cb(n->children[i], data);
        if (res) return res;
    }
    return 0;
}
