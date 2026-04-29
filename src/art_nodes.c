#include "art_nodes.h"

#include <stdlib.h>

/*
 * Allocates a node of the given type,
 * initializes to zero and sets the type.
 */
art_node* art_alloc_node(uint8_t type) {
    art_node *n;
    switch (type) {
        case NODE4:   n = (art_node*)calloc(1, sizeof(art_node4)); break;
        case NODE16:  n = (art_node*)calloc(1, sizeof(art_node16)); break;
        case NODE48:  n = (art_node*)calloc(1, sizeof(art_node48)); break;
        case NODE256: n = (art_node*)calloc(1, sizeof(art_node256)); break;
        default: abort();
    }
    n->type = type;
    return n;
}

/*
 * Dispatches the representation-specific child lookup.
 */
art_node** art_find_child(art_node *n, unsigned char c) {
    switch (n->type) {
        case NODE4: return art_node4_find_child((art_node4*)n, c);
        case NODE16: return art_node16_find_child((art_node16*)n, c);
        case NODE48: return art_node48_find_child((art_node48*)n, c);
        case NODE256: return art_node256_find_child((art_node256*)n, c);
        default: abort();
    }
}

/*
 * Dispatches child insertion, including node growth when full.
 */
void art_add_child(art_node *n, art_node **ref, unsigned char c, void *child) {
    switch (n->type) {
        case NODE4: art_node4_add_child((art_node4*)n, ref, c, child); return;
        case NODE16: art_node16_add_child((art_node16*)n, ref, c, child); return;
        case NODE48: art_node48_add_child((art_node48*)n, ref, c, child); return;
        case NODE256: art_node256_add_child((art_node256*)n, ref, c, child); return;
        default: abort();
    }
}

/*
 * Dispatches child removal, including node shrink/compression on underflow.
 */
void art_remove_child(art_node *n, art_node **ref, unsigned char c, art_node **slot) {
    switch (n->type) {
        case NODE4: art_node4_remove_child((art_node4*)n, ref, slot); return;
        case NODE16: art_node16_remove_child((art_node16*)n, ref, slot); return;
        case NODE48: art_node48_remove_child((art_node48*)n, ref, c); return;
        case NODE256: art_node256_remove_child((art_node256*)n, ref, c); return;
        default: abort();
    }
}

/*
 * Find the minimum child pointer under a node.
 */
art_node* art_first_child(const art_node *n) {
    switch (n->type) {
        case NODE4: return art_node4_first_child((const art_node4*)n);
        case NODE16: return art_node16_first_child((const art_node16*)n);
        case NODE48: return art_node48_first_child((const art_node48*)n);
        case NODE256: return art_node256_first_child((const art_node256*)n);
        default: abort();
    }
}

/*
 * Find the maximum child pointer under a node.
 */
art_node* art_last_child(const art_node *n) {
    switch (n->type) {
        case NODE4: return art_node4_last_child((const art_node4*)n);
        case NODE16: return art_node16_last_child((const art_node16*)n);
        case NODE48: return art_node48_last_child((const art_node48*)n);
        case NODE256: return art_node256_last_child((const art_node256*)n);
        default: abort();
    }
}

/*
 * Iterate children in key order.
 */
int art_for_each_child(art_node *n, art_child_cb cb, void *data) {
    switch (n->type) {
        case NODE4: return art_node4_for_each_child((art_node4*)n, cb, data);
        case NODE16: return art_node16_for_each_child((art_node16*)n, cb, data);
        case NODE48: return art_node48_for_each_child((art_node48*)n, cb, data);
        case NODE256: return art_node256_for_each_child((art_node256*)n, cb, data);
        default: abort();
    }
}
