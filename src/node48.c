#include "art_nodes.h"

#include <stdlib.h>

art_node** art_node48_find_child(art_node48 *n, unsigned char c) {
    int i = n->keys[c];
    return i ? &n->children[i - 1] : NULL;
}

void art_node48_add_child(art_node48 *n, unsigned char c, void *child) {
    if (n->n.num_children >= 48) abort();

    int pos = n->n.num_children;
    if (pos >= 48 || n->children[pos]) {
        pos = 0;
        while (n->children[pos]) pos++;
    }
    n->children[pos] = (art_node*)child;
    n->keys[c] = pos + 1;
    n->n.num_children++;
}

void art_node48_remove_child(art_node48 *n, unsigned char c) {
    int pos = n->keys[c];
    n->keys[c] = 0;
    n->children[pos - 1] = NULL;
    n->n.num_children--;
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
