#ifndef ART_NODE_MENU_H
#define ART_NODE_MENU_H

#include <stdint.h>

#if !defined(ART_MENU_ORIGINAL4) && \
    !defined(ART_MENU_NODE32_ARRAY) && \
    !defined(ART_MENU_NODE64_INDEXED) && \
    !defined(ART_MENU_NODE32_NODE64_INDEXED) && \
    !defined(ART_MENU_PAPER6_INDEXED) && \
    !defined(ART_MENU_NODE256_ONLY) && \
    !defined(ART_MENU_COUNT1_NODE256) && \
    !defined(ART_MENU_COUNT2_16_256) && \
    !defined(ART_MENU_COUNT3_4_16_256) && \
    !defined(ART_MENU_COUNT4_ORIGINAL4) && \
    !defined(ART_MENU_COUNT5_4_16_32_64_256) && \
    !defined(ART_MENU_COUNT6_4_16_32_48_64_256) && \
    !defined(ART_MENU_COUNT6_PAPER6) && \
    !defined(ART_MENU_COUNT7_2_5_16_32_48_64_256)
#if defined(ART_ENABLE_NODE32)
#define ART_MENU_NODE32_ARRAY
#else
#define ART_MENU_ORIGINAL4
#endif
#endif

#if defined(ART_MENU_PAPER6_INDEXED) || \
    defined(ART_MENU_COUNT6_PAPER6) || \
    defined(ART_MENU_COUNT7_2_5_16_32_48_64_256)
#define ART_HAS_NODE2 1
#define ART_HAS_NODE5 1
#else
#define ART_HAS_NODE2 0
#define ART_HAS_NODE5 0
#endif

#if defined(ART_MENU_ORIGINAL4) || \
    defined(ART_MENU_NODE32_ARRAY) || \
    defined(ART_MENU_NODE64_INDEXED) || \
    defined(ART_MENU_NODE32_NODE64_INDEXED) || \
    defined(ART_MENU_COUNT3_4_16_256) || \
    defined(ART_MENU_COUNT4_ORIGINAL4) || \
    defined(ART_MENU_COUNT5_4_16_32_64_256) || \
    defined(ART_MENU_COUNT6_4_16_32_48_64_256)
#define ART_HAS_NODE4 1
#else
#define ART_HAS_NODE4 0
#endif

#if defined(ART_MENU_NODE256_ONLY) || defined(ART_MENU_COUNT1_NODE256)
#define ART_HAS_NODE16 0
#else
#define ART_HAS_NODE16 1
#endif

#if defined(ART_MENU_NODE32_ARRAY) || \
    defined(ART_MENU_NODE32_NODE64_INDEXED) || \
    defined(ART_MENU_PAPER6_INDEXED) || \
    defined(ART_MENU_COUNT5_4_16_32_64_256) || \
    defined(ART_MENU_COUNT6_4_16_32_48_64_256) || \
    defined(ART_MENU_COUNT6_PAPER6) || \
    defined(ART_MENU_COUNT7_2_5_16_32_48_64_256)
#define ART_HAS_NODE32 1
#else
#define ART_HAS_NODE32 0
#endif

#if defined(ART_MENU_ORIGINAL4) || \
    defined(ART_MENU_NODE32_ARRAY) || \
    defined(ART_MENU_NODE64_INDEXED) || \
    defined(ART_MENU_NODE32_NODE64_INDEXED) || \
    defined(ART_MENU_COUNT4_ORIGINAL4) || \
    defined(ART_MENU_COUNT6_4_16_32_48_64_256) || \
    defined(ART_MENU_COUNT7_2_5_16_32_48_64_256)
#define ART_HAS_NODE48 1
#else
#define ART_HAS_NODE48 0
#endif

#if defined(ART_MENU_NODE64_INDEXED) || \
    defined(ART_MENU_NODE32_NODE64_INDEXED) || \
    defined(ART_MENU_PAPER6_INDEXED) || \
    defined(ART_MENU_COUNT5_4_16_32_64_256) || \
    defined(ART_MENU_COUNT6_4_16_32_48_64_256) || \
    defined(ART_MENU_COUNT6_PAPER6) || \
    defined(ART_MENU_COUNT7_2_5_16_32_48_64_256)
#define ART_HAS_NODE64 1
#else
#define ART_HAS_NODE64 0
#endif

static inline uint8_t art_menu_min_type(void) {
#if defined(ART_MENU_NODE256_ONLY) || defined(ART_MENU_COUNT1_NODE256)
    return NODE256;
#elif defined(ART_MENU_COUNT2_16_256)
    return NODE16;
#elif ART_HAS_NODE2
    return NODE2;
#else
    return NODE4;
#endif
}

static inline int art_node_type_enabled(uint8_t type) {
    switch (type) {
#if defined(ART_MENU_NODE256_ONLY) || defined(ART_MENU_COUNT1_NODE256)
        case NODE256:
            return 1;
#else
#if ART_HAS_NODE2
        case NODE2:
            return 1;
#endif
#if ART_HAS_NODE16
        case NODE16:
            return 1;
#endif
#if ART_HAS_NODE4
        case NODE4:
            return 1;
#endif
#if ART_HAS_NODE5
        case NODE5:
            return 1;
#endif
#if ART_HAS_NODE32
        case NODE32:
            return 1;
#endif
#if ART_HAS_NODE48
        case NODE48:
            return 1;
#endif
#if ART_HAS_NODE64
        case NODE64:
            return 1;
#endif
        case NODE256:
            return 1;
#endif
        default:
            return 0;
    }
}

static inline unsigned art_node_capacity(uint8_t type) {
    switch (type) {
#if ART_HAS_NODE2
        case NODE2: return 2;
#endif
#if ART_HAS_NODE4
        case NODE4: return 4;
#endif
#if ART_HAS_NODE5
        case NODE5: return 5;
#endif
#if ART_HAS_NODE16
        case NODE16: return 16;
#endif
#if ART_HAS_NODE32
        case NODE32: return 32;
#endif
#if ART_HAS_NODE48
        case NODE48: return 48;
#endif
#if ART_HAS_NODE64
        case NODE64: return 64;
#endif
        case NODE256: return 256;
        default: return 0;
    }
}

static inline uint8_t art_menu_next_type(uint8_t type) {
#if defined(ART_MENU_NODE256_ONLY) || defined(ART_MENU_COUNT1_NODE256)
    (void)type;
    return 0;
#else
    switch (type) {
        case NODE2: return NODE5;
        case NODE5: return NODE16;
        case NODE4: return NODE16;
        case NODE16:
#if ART_HAS_NODE32
            return NODE32;
#elif ART_HAS_NODE48
            return NODE48;
#else
            return NODE256;
#endif
#if ART_HAS_NODE32
        case NODE32:
#if ART_HAS_NODE48
            return NODE48;
#elif ART_HAS_NODE64
            return NODE64;
#else
            return NODE256;
#endif
#endif
#if ART_HAS_NODE48
        case NODE48:
#if ART_HAS_NODE64
            return NODE64;
#else
            return NODE256;
#endif
#endif
#if ART_HAS_NODE64
        case NODE64: return NODE256;
#endif
        default: return 0;
    }
#endif
}

static inline uint8_t art_menu_shrink_type(uint8_t type, uint16_t child_count) {
#if defined(ART_MENU_NODE256_ONLY) || defined(ART_MENU_COUNT1_NODE256)
    (void)type;
    (void)child_count;
    return 0;
#else
    switch (type) {
#if ART_HAS_NODE5
        case NODE5:
            return child_count == 2 ? NODE2 : 0;
#endif
        case NODE16:
#if ART_HAS_NODE5
            return child_count == 5 ? NODE5 : 0;
#elif ART_HAS_NODE4
            return child_count == 3 ? NODE4 : 0;
#else
            return 0;
#endif
#if ART_HAS_NODE32
        case NODE32:
            return child_count == 12 ? NODE16 : 0;
#endif
#if ART_HAS_NODE48
        case NODE48:
#if ART_HAS_NODE32
            return child_count == 24 ? NODE32 : 0;
#else
            return child_count == 12 ? NODE16 : 0;
#endif
#endif
#if ART_HAS_NODE64
        case NODE64:
#if ART_HAS_NODE48
            return child_count == 37 ? NODE48 : 0;
#elif ART_HAS_NODE32
            return child_count == 24 ? NODE32 : 0;
#elif ART_HAS_NODE16
            return child_count == 12 ? NODE16 : 0;
#else
            return 0;
#endif
#endif
        case NODE256:
#if ART_HAS_NODE64
            return child_count == 49 ? NODE64 : 0;
#elif ART_HAS_NODE48
            return child_count == 37 ? NODE48 : 0;
#elif ART_HAS_NODE32
            return child_count == 24 ? NODE32 : 0;
#elif ART_HAS_NODE16
            return child_count == 12 ? NODE16 : 0;
#else
            return 0;
#endif
        default:
            return 0;
    }
#endif
}

#endif
