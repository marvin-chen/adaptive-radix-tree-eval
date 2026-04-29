#include "art_nodes.h"

#include <stdlib.h>

art_node** art_node48_find_child(art_node48 *n, unsigned char c) {
    int i = n->keys[c];
    return i ? &n->children[i - 1] : NULL;
}

void art_node48_add_child(art_node48 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 48) {
        int pos = 0;
        while (n->children[pos]) pos++;
        n->children[pos] = (art_node*)child;
        n->keys[c] = pos + 1;
        n->n.num_children++;
        return;
    }

    art_node256 *new_node = (art_node256*)art_alloc_node(NODE256);
    // Copy the child pointers into the direct-addressed node.
    for (int i = 0; i < 256; i++) {
        if (n->keys[i]) new_node->children[i] = n->children[n->keys[i] - 1];
    }
    art_copy_header((art_node*)new_node, (art_node*)n);
    *ref = (art_node*)new_node;
    free(n);
    art_add_child((art_node*)new_node, ref, c, child);
}

void art_node48_remove_child(art_node48 *n, art_node **ref, unsigned char c) {
    int pos = n->keys[c];
    n->keys[c] = 0;
    n->children[pos - 1] = NULL;
    n->n.num_children--;

    /*
     * Resize to a node16 on underflow, not immediately to prevent
     * trashing if we sit on the 16/17 boundary.
     */
    if (n->n.num_children == 12) {
        art_node16 *new_node = (art_node16*)art_alloc_node(NODE16);
        *ref = (art_node*)new_node;
        art_copy_header((art_node*)new_node, (art_node*)n);
        int child = 0;
        for (int i = 0; i < 256; i++) {
            pos = n->keys[i];
            if (pos) {
                new_node->keys[child] = i;
                new_node->children[child] = n->children[pos - 1];
                child++;
            }
        }
        free(n);
    }
}

art_node* art_node48_first_child(const art_node48 *n) {
    int idx = 0;
    while (!n->keys[idx]) idx++;
    return n->children[n->keys[idx] - 1];
}

art_node* art_node48_last_child(const art_node48 *n) {
    int idx = 255;
    while (!n->keys[idx]) idx--;
    return n->children[n->keys[idx] - 1];
}

int art_node48_for_each_child(art_node48 *n, art_child_cb cb, void *data) {
    for (int i = 0; i < 256; i++) {
        int idx = n->keys[i];
        if (!idx) continue;
        int res = cb(n->children[idx - 1], data);
        if (res) return res;
    }
    return 0;
}
