#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>

static constexpr int PREFIX_MAX = 8;

enum NodeType : uint8_t { N4, N16, N48, N256 };

struct NodeHeader {
    NodeType type;
    uint8_t  num_children;
    uint8_t  prefix_len;
    uint8_t  prefix[PREFIX_MAX];
    uint8_t  _pad[5];  // pad to 16 bytes to match paper Table I
};

// 52 bytes (16 hdr + 4 keys + 4*8 ptrs)
struct Node4 {
    NodeHeader hdr;
    uint8_t    keys[4];
    void*      children[4];
};

// 160 bytes (16 hdr + 16 keys + 16*8 ptrs)
struct Node16 {
    NodeHeader hdr;
    uint8_t    keys[16];
    void*      children[16];
};

// 656 bytes (16 hdr + 256 index + 48*8 ptrs)
struct Node48 {
    NodeHeader hdr;
    uint8_t    child_index[256];  // 255 = empty sentinel
    void*      children[48];
};

// 2064 bytes (16 hdr + 256*8 ptrs)
struct Node256 {
    NodeHeader hdr;
    void*      children[256];
};

inline bool  is_leaf(void* ptr)    { return (uintptr_t)ptr & 1; }
inline void* make_leaf(void* val)  { return (void*)((uintptr_t)val | 1); }
inline void* get_leaf(void* ptr)   { return (void*)((uintptr_t)ptr & ~1ULL); }

int         check_prefix(const NodeHeader* node, const uint8_t* key, int key_len, int depth);
void**      find_child(NodeHeader* node, uint8_t byte);
NodeHeader* grow(NodeHeader* node);
void*       lookup(NodeHeader* node, const uint8_t* key, int key_len, int depth);

struct ART {
    NodeHeader* root = nullptr;

    void   insert(const uint8_t* key, int key_len, void* value);
    void*  lookup(const uint8_t* key, int key_len);
    size_t memory_usage() const;
};