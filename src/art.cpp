// src/art.cpp
#include "art.h"
#include <algorithm>
#include <cstring>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define ART_HAS_X86_SIMD 1
#else
#define ART_HAS_X86_SIMD 0
#endif

// ─── Prefix check ───────────────────────────────────────────────────────────
// Returns how many bytes of node->prefix match key[depth..].
// Caps comparison at PREFIX_MAX (the pessimistic inline stored bytes).
int check_prefix(const NodeHeader* node, const uint8_t* key, int key_len, int depth) {
    int max_cmp = std::min((int)node->prefix_len, PREFIX_MAX);
    for (int i = 0; i < max_cmp; i++) {
        if (depth + i >= key_len)   return i;
        if (node->prefix[i] != key[depth + i]) return i;
    }
    return max_cmp;
}

// ─── find_child ─────────────────────────────────────────────────────────────
void** find_child(NodeHeader* node, uint8_t byte) {
    if (node->type == N4) {
        auto* n = (Node4*)node;
        for (int i = 0; i < node->num_children; i++)
            if (n->keys[i] == byte) return &n->children[i];
        return nullptr;
    }
    if (node->type == N16) {
        auto* n = (Node16*)node;
#if ART_HAS_X86_SIMD
        __m128i key  = _mm_set1_epi8((char)byte);
        __m128i keys = _mm_loadu_si128((__m128i*)n->keys);
        __m128i cmp  = _mm_cmpeq_epi8(key, keys);
        int mask     = _mm_movemask_epi8(cmp) & ((1 << node->num_children) - 1);
        if (mask) return &n->children[__builtin_ctz(mask)];
        return nullptr;
#else
        for (int i = 0; i < node->num_children; i++)
            if (n->keys[i] == byte) return &n->children[i];
        return nullptr;
#endif
    }
    if (node->type == N48) {
        auto* n = (Node48*)node;
        if (n->child_index[byte] != 255)
            return &n->children[n->child_index[byte]];
        return nullptr;
    }
    // N256: direct array lookup
    auto* n = (Node256*)node;
    return n->children[byte] ? &n->children[byte] : nullptr;
}

// ─── grow ────────────────────────────────────────────────────────────────────
NodeHeader* grow(NodeHeader* node) {
    if (node->type == N4) {
        auto* old = (Node4*)node;
        auto* n   = new Node16();
        n->hdr    = old->hdr;
        n->hdr.type = N16;
        memcpy(n->keys,     old->keys,     4);
        memcpy(n->children, old->children, 4 * sizeof(void*));
        delete old;
        return (NodeHeader*)n;
    }
    if (node->type == N16) {
        auto* old = (Node16*)node;
        auto* n   = new Node48();
        n->hdr    = old->hdr;
        n->hdr.type = N48;
        memset(n->child_index, 255, sizeof(n->child_index));
        for (int i = 0; i < 16; i++) {
            n->child_index[old->keys[i]] = i;
            n->children[i] = old->children[i];
        }
        delete old;
        return (NodeHeader*)n;
    }
    if (node->type == N48) {
        auto* old = (Node48*)node;
        auto* n   = new Node256();
        n->hdr    = old->hdr;
        n->hdr.type = N256;
        memset(n->children, 0, sizeof(n->children));
        for (int i = 0; i < 256; i++)
            if (old->child_index[i] != 255)
                n->children[i] = old->children[old->child_index[i]];
        delete old;
        return (NodeHeader*)n;
    }
    return node;  // N256 can't grow
}

static NodeHeader* make_node4() {
    auto* n = new Node4();
    n->hdr.type = N4;
    n->hdr.num_children = 0;
    n->hdr.prefix_len = 0;
    memset(n->hdr.prefix, 0, sizeof(n->hdr.prefix));
    memset(n->keys, 0, sizeof(n->keys));
    memset(n->children, 0, sizeof(n->children));
    return (NodeHeader*)n;
}

static void add_child(NodeHeader** node_ref, uint8_t key_byte, void* child) {
    NodeHeader* node = *node_ref;

    if (node->type == N4) {
        auto* n = (Node4*)node;
        if (node->num_children < 4) {
            int pos = node->num_children;
            n->keys[pos] = key_byte;
            n->children[pos] = child;
            node->num_children++;
            return;
        }
    } else if (node->type == N16) {
        auto* n = (Node16*)node;
        if (node->num_children < 16) {
            int pos = node->num_children;
            n->keys[pos] = key_byte;
            n->children[pos] = child;
            node->num_children++;
            return;
        }
    } else if (node->type == N48) {
        auto* n = (Node48*)node;
        if (node->num_children < 48) {
            int pos = node->num_children;
            n->child_index[key_byte] = (uint8_t)pos;
            n->children[pos] = child;
            node->num_children++;
            return;
        }
    } else {
        auto* n = (Node256*)node;
        if (!n->children[key_byte]) {
            node->num_children++;
        }
        n->children[key_byte] = child;
        return;
    }

    // Node is full: grow one level and retry insertion.
    node = grow(node);
    *node_ref = node;
    add_child(node_ref, key_byte, child);
}

static void* build_chain_to_leaf(const uint8_t* key, int key_len, void* tagged_leaf, int depth) {
    if (depth >= key_len) return tagged_leaf;

    NodeHeader* n = make_node4();
    void* child = build_chain_to_leaf(key, key_len, tagged_leaf, depth + 1);
    add_child(&n, key[depth], child);
    return n;
}

static void insert_recursive(NodeHeader** node_ref, const uint8_t* key, int key_len, void* tagged_leaf, int depth) {
    if (depth >= key_len) return;

    if (*node_ref == nullptr) {
        *node_ref = (NodeHeader*)build_chain_to_leaf(key, key_len, tagged_leaf, depth);
        return;
    }

    NodeHeader* node = *node_ref;
    uint8_t key_byte = key[depth];
    void** child_ptr = find_child(node, key_byte);

    if (!child_ptr) {
        void* child = build_chain_to_leaf(key, key_len, tagged_leaf, depth + 1);
        add_child(node_ref, key_byte, child);
        return;
    }

    void* child = *child_ptr;

    if (depth == key_len - 1) {
        // Same key inserted again: replace value.
        *child_ptr = tagged_leaf;
        return;
    }

    if (is_leaf(child)) {
        // This implementation does not track original key bytes for leaf split.
        // Fall back to replacement to keep behavior defined for this scaffold.
        *child_ptr = build_chain_to_leaf(key, key_len, tagged_leaf, depth + 1);
        return;
    }

    insert_recursive((NodeHeader**)child_ptr, key, key_len, tagged_leaf, depth + 1);
}

// ─── lookup ──────────────────────────────────────────────────────────────────
// is_leaf / get_leaf check the *child* slot, not `node` itself.
// Inner nodes are plain heap pointers; leaf values are tagged with bit 0.
void* lookup(NodeHeader* node, const uint8_t* key, int key_len, int depth) {
    while (node) {
        // 1. Check inline prefix
        int p = check_prefix(node, key, key_len, depth);
        if (p != node->prefix_len) return nullptr;  // prefix mismatch
        depth += node->prefix_len;

        // 2. If we've consumed the full key, the value lives in a special
        //    null-byte child (or this is a single-key tree). For simplicity,
        //    treat exhausted key as not found until you add that edge case.
        if (depth >= key_len) return nullptr;

        // 3. Descend one level
        void** child_ptr = find_child(node, key[depth]);
        if (!child_ptr) return nullptr;

        void* child = *child_ptr;
        depth++;

        // 4. Child is a tagged leaf pointer — check it and return
        if (is_leaf(child))
            return get_leaf(child);  // returns the stored value pointer

        // 5. Child is another inner node — keep descending
        node = (NodeHeader*)child;
    }
    return nullptr;
}

// ART member wrappers — these call the free functions above
void* ART::lookup(const uint8_t* key, int key_len) {
    return ::lookup(root, key, key_len, 0);
}

void ART::insert(const uint8_t* key, int key_len, void* value) {
    if (!key || key_len <= 0) return;
    insert_recursive(&root, key, key_len, make_leaf(value), 0);
}

size_t ART::memory_usage() const {
    // Stub — implement tree walk later
    return 0;
}