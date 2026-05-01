#include "art_nodes.h"

#include <stdlib.h>
#include <string.h>

static void art_add_child_local(art_node *n, unsigned char c, void *child);

/*
 * Allocates a node of the given type,
 * initializes to zero and sets the type.
 */
art_node* art_alloc_node(uint8_t type) {
    if (!art_node_type_enabled(type)) abort();

    art_node *n;
    switch (type) {
#if ART_HAS_NODE2
        case NODE2:   n = (art_node*)calloc(1, sizeof(art_node2)); break;
#endif
        case NODE4:   n = (art_node*)calloc(1, sizeof(art_node4)); break;
#if ART_HAS_NODE5
        case NODE5:   n = (art_node*)calloc(1, sizeof(art_node5)); break;
#endif
        case NODE16:  n = (art_node*)calloc(1, sizeof(art_node16)); break;
#if ART_HAS_NODE32
        case NODE32:  n = (art_node*)calloc(1, sizeof(art_node32)); break;
#endif
        case NODE48:  n = (art_node*)calloc(1, sizeof(art_node48)); break;
#if ART_HAS_NODE64
        case NODE64:  n = (art_node*)calloc(1, sizeof(art_node64)); break;
#endif
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
#if ART_HAS_NODE2
        case NODE2: return art_node2_find_child((art_node2*)n, c);
#endif
        case NODE4: return art_node4_find_child((art_node4*)n, c);
#if ART_HAS_NODE5
        case NODE5: return art_node5_find_child((art_node5*)n, c);
#endif
        case NODE16: return art_node16_find_child((art_node16*)n, c);
#if ART_HAS_NODE32
        case NODE32: return art_node32_find_child((art_node32*)n, c);
#endif
        case NODE48: return art_node48_find_child((art_node48*)n, c);
#if ART_HAS_NODE64
        case NODE64: return art_node64_find_child((art_node64*)n, c);
#endif
        case NODE256: return art_node256_find_child((art_node256*)n, c);
        default: abort();
    }
}

static void art_add_child_local(art_node *n, unsigned char c, void *child) {
    switch (n->type) {
#if ART_HAS_NODE2
        case NODE2: art_node2_add_child((art_node2*)n, c, child); return;
#endif
        case NODE4: art_node4_add_child((art_node4*)n, c, child); return;
#if ART_HAS_NODE5
        case NODE5: art_node5_add_child((art_node5*)n, c, child); return;
#endif
        case NODE16: art_node16_add_child((art_node16*)n, c, child); return;
#if ART_HAS_NODE32
        case NODE32: art_node32_add_child((art_node32*)n, c, child); return;
#endif
        case NODE48: art_node48_add_child((art_node48*)n, c, child); return;
#if ART_HAS_NODE64
        case NODE64: art_node64_add_child((art_node64*)n, c, child); return;
#endif
        case NODE256: art_node256_add_child((art_node256*)n, c, child); return;
        default: abort();
    }
}

static void art_copy_children(const art_node *src, art_node *dst) {
    switch (src->type) {
#if ART_HAS_NODE2
        case NODE2: {
            const art_node2 *n = (const art_node2*)src;
            for (int i = 0; i < n->n.num_children; i++) {
                art_add_child_local(dst, n->keys[i], n->children[i]);
            }
            return;
        }
#endif
        case NODE4: {
            const art_node4 *n = (const art_node4*)src;
            for (int i = 0; i < n->n.num_children; i++) {
                art_add_child_local(dst, n->keys[i], n->children[i]);
            }
            return;
        }
#if ART_HAS_NODE5
        case NODE5: {
            const art_node5 *n = (const art_node5*)src;
            for (int i = 0; i < n->n.num_children; i++) {
                art_add_child_local(dst, n->keys[i], n->children[i]);
            }
            return;
        }
#endif
        case NODE16: {
            const art_node16 *n = (const art_node16*)src;
            for (int i = 0; i < n->n.num_children; i++) {
                art_add_child_local(dst, n->keys[i], n->children[i]);
            }
            return;
        }
#if ART_HAS_NODE32
        case NODE32: {
            const art_node32 *n = (const art_node32*)src;
            for (int i = 0; i < n->n.num_children; i++) {
                art_add_child_local(dst, n->keys[i], n->children[i]);
            }
            return;
        }
#endif
        case NODE48: {
            const art_node48 *n = (const art_node48*)src;
            for (int i = 0; i < 256; i++) {
                int pos = n->keys[i];
                if (pos) art_add_child_local(dst, (unsigned char)i, n->children[pos - 1]);
            }
            return;
        }
#if ART_HAS_NODE64
        case NODE64: {
            const art_node64 *n = (const art_node64*)src;
#if ART_NODE64_INDEXED
            for (int i = 0; i < 256; i++) {
                int pos = n->keys[i];
                if (pos) art_add_child_local(dst, (unsigned char)i, n->children[pos - 1]);
            }
#else
            int child = 0;
            for (int i = 0; i < 256; i++) {
                if (n->bitmap[i >> 6] & (UINT64_C(1) << (i & 63))) {
                    art_add_child_local(dst, (unsigned char)i, n->children[child++]);
                }
            }
#endif
            return;
        }
#endif
        case NODE256: {
            const art_node256 *n = (const art_node256*)src;
            for (int i = 0; i < 256; i++) {
                if (n->children[i]) art_add_child_local(dst, (unsigned char)i, n->children[i]);
            }
            return;
        }
        default:
            abort();
    }
}

static art_node* art_replace_node(art_node *old_node, uint8_t new_type) {
    art_node *new_node = art_alloc_node(new_type);
    art_copy_header(new_node, old_node);
    new_node->num_children = 0;
    art_copy_children(old_node, new_node);
    return new_node;
}

static art_node* art_grow_node(art_node *n, art_node **ref) {
    uint8_t next_type = art_menu_next_type(n->type);
    if (!next_type) abort();

    art_node *new_node = art_replace_node(n, next_type);
    *ref = new_node;
    free(n);
    return new_node;
}

static void art_shrink_node(art_node *n, art_node **ref) {
    uint8_t shrink_type = art_menu_shrink_type(n->type, n->num_children);
    if (!shrink_type) return;

    art_node *new_node = art_replace_node(n, shrink_type);
    *ref = new_node;
    free(n);
}

void art_compress_single_child(art_node *n, art_node **ref, unsigned char key, art_node *child) {
    if (!IS_LEAF(child)) {
        // Concatenate the prefixes.
        int prefix = n->partial_len;
        if (prefix < MAX_PREFIX_LEN) n->partial[prefix++] = key;
        if (prefix < MAX_PREFIX_LEN) {
            int sub_prefix = art_min_int(child->partial_len, MAX_PREFIX_LEN - prefix);
            memcpy(n->partial + prefix, child->partial, sub_prefix);
            prefix += sub_prefix;
        }

        // Store the prefix in the child.
        memcpy(child->partial, n->partial, art_min_int(prefix, MAX_PREFIX_LEN));
        child->partial_len += n->partial_len + 1;
    }
    *ref = child;
    free(n);
}

/*
 * Dispatches child insertion, including menu-selected node growth when full.
 */
void art_add_child(art_node *n, art_node **ref, unsigned char c, void *child) {
    if (n->num_children >= art_node_capacity(n->type)) {
        n = art_grow_node(n, ref);
    }
    art_add_child_local(n, c, child);
}

/*
 * Dispatches child removal, including menu-selected node shrink on underflow.
 */
void art_remove_child(art_node *n, art_node **ref, unsigned char c, art_node **slot) {
    switch (n->type) {
#if ART_HAS_NODE2
        case NODE2:
            art_node2_remove_child((art_node2*)n, ref, slot);
            return;
#endif
        case NODE4:
            art_node4_remove_child((art_node4*)n, ref, slot);
            return;
#if ART_HAS_NODE5
        case NODE5:
            art_node5_remove_child((art_node5*)n, slot);
            break;
#endif
        case NODE16:
            art_node16_remove_child((art_node16*)n, slot);
            break;
#if ART_HAS_NODE32
        case NODE32:
            art_node32_remove_child((art_node32*)n, slot);
            break;
#endif
        case NODE48:
            art_node48_remove_child((art_node48*)n, c);
            break;
#if ART_HAS_NODE64
        case NODE64:
            art_node64_remove_child((art_node64*)n, c);
            break;
#endif
        case NODE256:
            art_node256_remove_child((art_node256*)n, c);
            break;
        default:
            abort();
    }
    art_shrink_node(n, ref);
}

/*
 * Find the minimum child pointer under a node.
 */
art_node* art_first_child(const art_node *n) {
    switch (n->type) {
#if ART_HAS_NODE2
        case NODE2: return art_node2_first_child((const art_node2*)n);
#endif
        case NODE4: return art_node4_first_child((const art_node4*)n);
#if ART_HAS_NODE5
        case NODE5: return art_node5_first_child((const art_node5*)n);
#endif
        case NODE16: return art_node16_first_child((const art_node16*)n);
#if ART_HAS_NODE32
        case NODE32: return art_node32_first_child((const art_node32*)n);
#endif
        case NODE48: return art_node48_first_child((const art_node48*)n);
#if ART_HAS_NODE64
        case NODE64: return art_node64_first_child((const art_node64*)n);
#endif
        case NODE256: return art_node256_first_child((const art_node256*)n);
        default: abort();
    }
}

/*
 * Find the maximum child pointer under a node.
 */
art_node* art_last_child(const art_node *n) {
    switch (n->type) {
#if ART_HAS_NODE2
        case NODE2: return art_node2_last_child((const art_node2*)n);
#endif
        case NODE4: return art_node4_last_child((const art_node4*)n);
#if ART_HAS_NODE5
        case NODE5: return art_node5_last_child((const art_node5*)n);
#endif
        case NODE16: return art_node16_last_child((const art_node16*)n);
#if ART_HAS_NODE32
        case NODE32: return art_node32_last_child((const art_node32*)n);
#endif
        case NODE48: return art_node48_last_child((const art_node48*)n);
#if ART_HAS_NODE64
        case NODE64: return art_node64_last_child((const art_node64*)n);
#endif
        case NODE256: return art_node256_last_child((const art_node256*)n);
        default: abort();
    }
}

/*
 * Iterate children in key order.
 */
int art_for_each_child(art_node *n, art_child_cb cb, void *data) {
    switch (n->type) {
#if ART_HAS_NODE2
        case NODE2: return art_node2_for_each_child((art_node2*)n, cb, data);
#endif
        case NODE4: return art_node4_for_each_child((art_node4*)n, cb, data);
#if ART_HAS_NODE5
        case NODE5: return art_node5_for_each_child((art_node5*)n, cb, data);
#endif
        case NODE16: return art_node16_for_each_child((art_node16*)n, cb, data);
#if ART_HAS_NODE32
        case NODE32: return art_node32_for_each_child((art_node32*)n, cb, data);
#endif
        case NODE48: return art_node48_for_each_child((art_node48*)n, cb, data);
#if ART_HAS_NODE64
        case NODE64: return art_node64_for_each_child((art_node64*)n, cb, data);
#endif
        case NODE256: return art_node256_for_each_child((art_node256*)n, cb, data);
        default: abort();
    }
}
