#include "art_nodes.h"

#include <stdlib.h>

art_node** art_node256_find_child(art_node256 *n, unsigned char c) {
    return n->children[c] ? &n->children[c] : NULL;
}

void art_node256_add_child(art_node256 *n, unsigned char c, void *child) {
    n->n.num_children++;
    n->children[c] = (art_node*)child;
}

void art_node256_remove_child(art_node256 *n, unsigned char c) {
    n->children[c] = NULL;
    n->n.num_children--;
}

art_node* art_node256_first_child(const art_node256 *n) {
    int idx = 0;
    while (!n->children[idx]) idx++;
    return n->children[idx];
}

art_node* art_node256_last_child(const art_node256 *n) {
    int idx = 255;
    while (!n->children[idx]) idx--;
    return n->children[idx];
}

int art_node256_for_each_child(art_node256 *n, art_child_cb cb, void *data) {
    for (int i = 0; i < 256; i++) {
        if (!n->children[i]) continue;
        int res = cb(n->children[i], data);
        if (res) return res;
    }
    return 0;
}
