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

void art_node4_add_child(art_node4 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 4) {
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
        return;
    }

    art_node16 *new_node = (art_node16*)art_alloc_node(NODE16);
    // Copy the child pointers and the key map.
    memcpy(new_node->children, n->children, sizeof(void*) * n->n.num_children);
    memcpy(new_node->keys, n->keys, sizeof(unsigned char) * n->n.num_children);
    art_copy_header((art_node*)new_node, (art_node*)n);
    *ref = (art_node*)new_node;
    free(n);
    art_add_child((art_node*)new_node, ref, c, child);
}

void art_node4_remove_child(art_node4 *n, art_node **ref, art_node **slot) {
    int pos = slot - n->children;
    memmove(n->keys + pos, n->keys + pos + 1, n->n.num_children - 1 - pos);
    memmove(n->children + pos, n->children + pos + 1,
            (n->n.num_children - 1 - pos) * sizeof(void*));
    n->n.num_children--;

    // Remove nodes with only a single child.
    if (n->n.num_children == 1) {
        art_node *child = n->children[0];
        if (!IS_LEAF(child)) {
            // Concatenate the prefixes.
            int prefix = n->n.partial_len;
            if (prefix < MAX_PREFIX_LEN) n->n.partial[prefix++] = n->keys[0];
            if (prefix < MAX_PREFIX_LEN) {
                int sub_prefix = art_min_int(child->partial_len, MAX_PREFIX_LEN - prefix);
                memcpy(n->n.partial + prefix, child->partial, sub_prefix);
                prefix += sub_prefix;
            }

            // Store the prefix in the child.
            memcpy(child->partial, n->n.partial, art_min_int(prefix, MAX_PREFIX_LEN));
            child->partial_len += n->n.partial_len + 1;
        }
        *ref = child;
        free(n);
    }
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
