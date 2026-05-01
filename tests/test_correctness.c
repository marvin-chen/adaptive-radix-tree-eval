#include "art.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ART_PROJECT_SOURCE_DIR
#define ART_PROJECT_SOURCE_DIR "."
#endif

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static void encode_u64_be(uint64_t value, unsigned char out[8]) {
    for (int i = 7; i >= 0; --i) {
        out[i] = (unsigned char)(value & 0xffu);
        value >>= 8;
    }
}

static void test_dense_uint64(void) {
    enum { N = 10000 };
    art_tree t;
    uintptr_t values[N];
    assert(art_tree_init(&t) == 0);

    for (uint64_t i = 0; i < N; ++i) {
        unsigned char key[8];
        values[i] = i + 1;
        encode_u64_be(i, key);
        assert(art_insert(&t, key, 8, &values[i]) == NULL);
    }
    assert(art_size(&t) == N);

    for (uint64_t i = 0; i < N; ++i) {
        unsigned char key[8];
        encode_u64_be(i, key);
        assert(art_search(&t, key, 8) == &values[i]);
    }

    unsigned char missing[8];
    encode_u64_be(N + 1, missing);
    assert(art_search(&t, missing, 8) == NULL);
    assert(art_tree_destroy(&t) == 0);
}

static void test_sparse_uint64(void) {
    enum { N = 10000 };
    art_tree t;
    uintptr_t values[N];
    assert(art_tree_init(&t) == 0);

    for (uint64_t i = 0; i < N; ++i) {
        uint64_t state = i + UINT64_C(12345);
        uint64_t value = splitmix64(&state) << 1;
        unsigned char key[8];
        values[i] = i + 1;
        encode_u64_be(value, key);
        assert(art_insert(&t, key, 8, &values[i]) == NULL);
    }

    for (uint64_t i = 0; i < N; ++i) {
        uint64_t state = i + UINT64_C(12345);
        uint64_t value = splitmix64(&state) << 1;
        unsigned char key[8];
        encode_u64_be(value, key);
        assert(art_search(&t, key, 8) == &values[i]);
    }

    for (uint64_t i = 0; i < 128; ++i) {
        uint64_t state = i + UINT64_C(67890);
        uint64_t value = (splitmix64(&state) << 1) | 1u;
        unsigned char key[8];
        encode_u64_be(value, key);
        assert(art_search(&t, key, 8) == NULL);
    }

    assert(art_tree_destroy(&t) == 0);
}

static void test_overwrite(void) {
    art_tree t;
    uintptr_t first = 1;
    uintptr_t second = 2;
    unsigned char key[8];
    encode_u64_be(42, key);

    assert(art_tree_init(&t) == 0);
    assert(art_insert(&t, key, 8, &first) == NULL);
    assert(art_insert(&t, key, 8, &second) == &first);
    assert(art_size(&t) == 1);
    assert(art_search(&t, key, 8) == &second);
    assert(art_tree_destroy(&t) == 0);
}

static void test_medium_fanout_growth_and_shrink(void) {
    enum { N = 40, REMAINING = 12 };
    art_tree t;
    uintptr_t values[N];
    assert(art_tree_init(&t) == 0);

    for (int i = 0; i < N; ++i) {
        unsigned char key[3] = { 0x42, (unsigned char)(i + 1), 0 };
        values[i] = (uintptr_t)(i + 1);
        assert(art_insert(&t, key, sizeof(key), &values[i]) == NULL);
    }
    assert(art_size(&t) == N);

    for (int i = 0; i < N; ++i) {
        unsigned char key[3] = { 0x42, (unsigned char)(i + 1), 0 };
        assert(art_search(&t, key, sizeof(key)) == &values[i]);
    }

    art_stats stats;
    assert(art_collect_stats(&t, &stats) == 0);
    assert(stats.keys == N);
    assert(stats.leaf_count == N);
    assert(stats.internal_node_count > 0);
    assert(stats.total_bytes == stats.internal_node_bytes + stats.leaf_bytes);
    assert(stats.fanout_hist[40] == 1);

    for (int i = N - 1; i >= REMAINING; --i) {
        unsigned char key[3] = { 0x42, (unsigned char)(i + 1), 0 };
        assert(art_delete(&t, key, sizeof(key)) == &values[i]);
        assert(art_search(&t, key, sizeof(key)) == NULL);
    }
    assert(art_size(&t) == REMAINING);
    assert(art_collect_stats(&t, &stats) == 0);
    assert(stats.keys == REMAINING);
    assert(stats.leaf_count == REMAINING);

    for (int i = 0; i < REMAINING; ++i) {
        unsigned char key[3] = { 0x42, (unsigned char)(i + 1), 0 };
        assert(art_search(&t, key, sizeof(key)) == &values[i]);
    }
    assert(art_tree_destroy(&t) == 0);
}

static void test_node32_target_fanout(void) {
    enum { N = 24 };
    art_tree t;
    uintptr_t values[N];
    assert(art_tree_init(&t) == 0);

    for (int i = 0; i < N; ++i) {
        unsigned char key[3] = { 0x32, (unsigned char)(i + 1), 0 };
        values[i] = (uintptr_t)(i + 1);
        assert(art_insert(&t, key, sizeof(key), &values[i]) == NULL);
    }

    art_stats stats;
    assert(art_collect_stats(&t, &stats) == 0);
    assert(stats.fanout_hist[N] == 1);
#ifdef ART_EXPECT_NODE32
    assert(stats.node32_count > 0);
#endif

    for (int i = 0; i < N; ++i) {
        unsigned char key[3] = { 0x32, (unsigned char)(i + 1), 0 };
        assert(art_search(&t, key, sizeof(key)) == &values[i]);
    }
    assert(art_tree_destroy(&t) == 0);
}

static void test_paper6_small_nodes(void) {
#if defined(ART_EXPECT_NODE2) && defined(ART_EXPECT_NODE5)
    {
        enum { N = 2 };
        art_tree t;
        uintptr_t values[N];
        assert(art_tree_init(&t) == 0);

        for (int i = 0; i < N; ++i) {
            unsigned char key[3] = { 0x25, (unsigned char)(i + 1), 0 };
            values[i] = (uintptr_t)(i + 1);
            assert(art_insert(&t, key, sizeof(key), &values[i]) == NULL);
        }

        art_stats stats;
        assert(art_collect_stats(&t, &stats) == 0);
        assert(stats.fanout_hist[N] == 1);
        assert(stats.node2_count > 0);
        assert(art_tree_destroy(&t) == 0);
    }

    {
        enum { N = 5 };
        art_tree t;
        uintptr_t values[N];
        assert(art_tree_init(&t) == 0);

        for (int i = 0; i < N; ++i) {
            unsigned char key[3] = { 0x55, (unsigned char)(i + 1), 0 };
            values[i] = (uintptr_t)(i + 1);
            assert(art_insert(&t, key, sizeof(key), &values[i]) == NULL);
        }

        art_stats stats;
        assert(art_collect_stats(&t, &stats) == 0);
        assert(stats.fanout_hist[N] == 1);
        assert(stats.node5_count > 0);
        assert(art_tree_destroy(&t) == 0);
    }
#endif
}

static void test_paper6_small_shrink(void) {
#if defined(ART_EXPECT_NODE2) && defined(ART_EXPECT_NODE5)
    enum { N = 16 };
    art_tree t;
    uintptr_t values[N];
    assert(art_tree_init(&t) == 0);

    for (int i = 0; i < N; ++i) {
        unsigned char key[3] = { 0x65, (unsigned char)(i + 1), 0 };
        values[i] = (uintptr_t)(i + 1);
        assert(art_insert(&t, key, sizeof(key), &values[i]) == NULL);
    }

    for (int i = N - 1; i >= 5; --i) {
        unsigned char key[3] = { 0x65, (unsigned char)(i + 1), 0 };
        assert(art_delete(&t, key, sizeof(key)) == &values[i]);
    }

    art_stats stats;
    assert(art_collect_stats(&t, &stats) == 0);
    assert(stats.fanout_hist[5] == 1);
    assert(stats.node5_count > 0);

    for (int i = 4; i >= 2; --i) {
        unsigned char key[3] = { 0x65, (unsigned char)(i + 1), 0 };
        assert(art_delete(&t, key, sizeof(key)) == &values[i]);
    }

    assert(art_collect_stats(&t, &stats) == 0);
    assert(stats.fanout_hist[2] == 1);
    assert(stats.node2_count > 0);

    for (int i = 0; i < 2; ++i) {
        unsigned char key[3] = { 0x65, (unsigned char)(i + 1), 0 };
        assert(art_search(&t, key, sizeof(key)) == &values[i]);
    }

    unsigned char deleted[3] = { 0x65, 16, 0 };
    assert(art_search(&t, deleted, sizeof(deleted)) == NULL);
    assert(art_tree_destroy(&t) == 0);
#endif
}

static void test_high_fanout_growth_and_shrink(void) {
    enum { N = 64, REMAINING = 36 };
    art_tree t;
    uintptr_t values[N];
    assert(art_tree_init(&t) == 0);

    for (int i = 0; i < N; ++i) {
        unsigned char key[3] = { 0x64, (unsigned char)(i + 1), 0 };
        values[i] = (uintptr_t)(i + 1);
        assert(art_insert(&t, key, sizeof(key), &values[i]) == NULL);
    }
    assert(art_size(&t) == N);

    art_stats stats;
    assert(art_collect_stats(&t, &stats) == 0);
    assert(stats.fanout_hist[64] == 1);
#ifdef ART_EXPECT_NODE64
    assert(stats.node64_count > 0);
#endif

    for (int i = 0; i < N; ++i) {
        unsigned char key[3] = { 0x64, (unsigned char)(i + 1), 0 };
        assert(art_search(&t, key, sizeof(key)) == &values[i]);
    }

    for (int i = N - 1; i >= REMAINING; --i) {
        unsigned char key[3] = { 0x64, (unsigned char)(i + 1), 0 };
        assert(art_delete(&t, key, sizeof(key)) == &values[i]);
        assert(art_search(&t, key, sizeof(key)) == NULL);
    }
    assert(art_size(&t) == REMAINING);

    for (int i = 0; i < REMAINING; ++i) {
        unsigned char key[3] = { 0x64, (unsigned char)(i + 1), 0 };
        assert(art_search(&t, key, sizeof(key)) == &values[i]);
    }
    assert(art_collect_stats(&t, &stats) == 0);
    assert(stats.fanout_hist[REMAINING] == 1);
    assert(art_tree_destroy(&t) == 0);
}

static void test_node256_only_targets(void) {
#ifdef ART_EXPECT_NODE256_ONLY
    for (int pass = 0; pass < 2; ++pass) {
        int n = pass == 0 ? 2 : 64;
        art_tree t;
        uintptr_t values[64];
        assert(art_tree_init(&t) == 0);

        for (int i = 0; i < n; ++i) {
            unsigned char key[3] = { 0x56, (unsigned char)(i + 1), 0 };
            values[i] = (uintptr_t)(i + 1);
            assert(art_insert(&t, key, sizeof(key), &values[i]) == NULL);
        }

        art_stats stats;
        assert(art_collect_stats(&t, &stats) == 0);
        assert(stats.node256_count > 0);
        assert(stats.node2_count == 0);
        assert(stats.node4_count == 0);
        assert(stats.node5_count == 0);
        assert(stats.node16_count == 0);
        assert(stats.node32_count == 0);
        assert(stats.node48_count == 0);
        assert(stats.node64_count == 0);
        assert(art_tree_destroy(&t) == 0);
    }

    {
        enum { N = 2 };
        art_tree t;
        uintptr_t values[N];
        assert(art_tree_init(&t) == 0);

        for (int i = 0; i < N; ++i) {
            unsigned char key[3] = { 0x57, (unsigned char)(i + 1), 0 };
            values[i] = (uintptr_t)(i + 1);
            assert(art_insert(&t, key, sizeof(key), &values[i]) == NULL);
        }
        for (int i = 0; i < N; ++i) {
            unsigned char key[3] = { 0x57, (unsigned char)(i + 1), 0 };
            assert(art_delete(&t, key, sizeof(key)) == &values[i]);
        }

        assert(art_size(&t) == 0);
        assert(art_minimum(&t) == NULL);
        assert(art_maximum(&t) == NULL);
        assert(art_tree_destroy(&t) == 0);
    }
#endif
}

static void build_shared_fanout(art_tree *t, uintptr_t *values, int n, unsigned char prefix) {
    assert(art_tree_init(t) == 0);
    for (int i = 0; i < n; ++i) {
        unsigned char key[3] = { prefix, (unsigned char)(i + 1), 0 };
        values[i] = (uintptr_t)(i + 1);
        assert(art_insert(t, key, sizeof(key), &values[i]) == NULL);
    }
}

static void assert_shared_lookup(art_tree *t, uintptr_t *values, int n, unsigned char prefix) {
    for (int i = 0; i < n; ++i) {
        unsigned char key[3] = { prefix, (unsigned char)(i + 1), 0 };
        assert(art_search(t, key, sizeof(key)) == &values[i]);
    }
}

static void test_count2_menu_targets(void) {
#ifdef ART_EXPECT_COUNT2_16_256
    for (int pass = 0; pass < 3; ++pass) {
        int n = pass == 0 ? 2 : pass == 1 ? 16 : 17;
        art_tree t;
        uintptr_t values[17];
        art_stats stats;
        build_shared_fanout(&t, values, n, (unsigned char)(0xa0 + pass));
        assert(art_collect_stats(&t, &stats) == 0);
        assert(stats.fanout_hist[n] == 1);
        if (n <= 16) {
            assert(stats.node16_count > 0);
            assert(stats.node256_count == 0);
        } else {
            assert(stats.node256_count > 0);
        }
        assert_shared_lookup(&t, values, n, (unsigned char)(0xa0 + pass));
        assert(art_tree_destroy(&t) == 0);
    }

    {
        enum { N = 17 };
        art_tree t;
        uintptr_t values[N];
        art_stats stats;
        build_shared_fanout(&t, values, N, 0xa4);
        for (int i = N - 1; i >= 1; --i) {
            unsigned char key[3] = { 0xa4, (unsigned char)(i + 1), 0 };
            assert(art_delete(&t, key, sizeof(key)) == &values[i]);
        }
        assert(art_size(&t) == 1);
        assert(art_collect_stats(&t, &stats) == 0);
        assert(stats.internal_node_count == 0);
        assert_shared_lookup(&t, values, 1, 0xa4);
        assert(art_tree_destroy(&t) == 0);
    }
#endif
}

static void test_count3_menu_targets(void) {
#ifdef ART_EXPECT_COUNT3_4_16_256
    int ns[] = { 4, 16, 17 };
    for (int pass = 0; pass < 3; ++pass) {
        int n = ns[pass];
        art_tree t;
        uintptr_t values[17];
        art_stats stats;
        build_shared_fanout(&t, values, n, (unsigned char)(0xb0 + pass));
        assert(art_collect_stats(&t, &stats) == 0);
        assert(stats.fanout_hist[n] == 1);
        if (n == 4) assert(stats.node4_count > 0);
        if (n == 16) assert(stats.node16_count > 0);
        if (n == 17) assert(stats.node256_count > 0);
        assert_shared_lookup(&t, values, n, (unsigned char)(0xb0 + pass));
        assert(art_tree_destroy(&t) == 0);
    }
#endif
}

static void test_count5_menu_targets(void) {
#ifdef ART_EXPECT_COUNT5_4_16_32_64_256
    int ns[] = { 24, 40, 48, 65 };
    for (int pass = 0; pass < 4; ++pass) {
        int n = ns[pass];
        art_tree t;
        uintptr_t values[65];
        art_stats stats;
        build_shared_fanout(&t, values, n, (unsigned char)(0xc0 + pass));
        assert(art_collect_stats(&t, &stats) == 0);
        assert(stats.fanout_hist[n] == 1);
        if (n == 24) assert(stats.node32_count > 0);
        if (n == 40 || n == 48) {
            assert(stats.node64_count > 0);
            assert(stats.node48_count == 0);
        }
        if (n == 65) assert(stats.node256_count > 0);
        assert_shared_lookup(&t, values, n, (unsigned char)(0xc0 + pass));
        assert(art_tree_destroy(&t) == 0);
    }
#endif
}

static void test_count7_menu_targets(void) {
#ifdef ART_EXPECT_COUNT7_2_5_16_32_48_64_256
    int ns[] = { 2, 5, 24, 40, 64 };
    for (int pass = 0; pass < 5; ++pass) {
        int n = ns[pass];
        art_tree t;
        uintptr_t values[64];
        art_stats stats;
        build_shared_fanout(&t, values, n, (unsigned char)(0xd0 + pass));
        assert(art_collect_stats(&t, &stats) == 0);
        assert(stats.fanout_hist[n] == 1);
        if (n == 2) assert(stats.node2_count > 0);
        if (n == 5) assert(stats.node5_count > 0);
        if (n == 24) assert(stats.node32_count > 0);
        if (n == 40) assert(stats.node48_count > 0);
        if (n == 64) assert(stats.node64_count > 0);
        assert_shared_lookup(&t, values, n, (unsigned char)(0xd0 + pass));
        assert(art_tree_destroy(&t) == 0);
    }
#endif
}

static void test_words_fixture_smoke(void) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/tests/words.txt", ART_PROJECT_SOURCE_DIR);
    FILE *f = fopen(path, "rb");
    assert(f != NULL);

    enum { MAX_WORDS = 256 };
    art_tree t;
    uintptr_t values[MAX_WORDS];
    char words[MAX_WORDS][256];
    int lengths[MAX_WORDS];
    int count = 0;
    assert(art_tree_init(&t) == 0);

    while (count < MAX_WORDS && fgets(words[count], sizeof(words[count]), f)) {
        size_t len = strlen(words[count]);
        while (len > 0 && (words[count][len - 1] == '\n' || words[count][len - 1] == '\r')) {
            words[count][--len] = '\0';
        }
        if (len == 0) continue;
        values[count] = (uintptr_t)(count + 1);
        lengths[count] = (int)len + 1;
        assert(art_insert(&t, (unsigned char*)words[count], lengths[count], &values[count]) == NULL);
        count++;
    }
    fclose(f);
    assert(count > 0);

    for (int i = 0; i < count; ++i) {
        assert(art_search(&t, (unsigned char*)words[i], lengths[i]) == &values[i]);
    }
    assert(art_tree_destroy(&t) == 0);
}

int main(void) {
    test_dense_uint64();
    test_sparse_uint64();
    test_overwrite();
    test_medium_fanout_growth_and_shrink();
    test_node32_target_fanout();
    test_paper6_small_nodes();
    test_paper6_small_shrink();
    test_high_fanout_growth_and_shrink();
    test_node256_only_targets();
    test_count2_menu_targets();
    test_count3_menu_targets();
    test_count5_menu_targets();
    test_count7_menu_targets();
    test_words_fixture_smoke();
    puts("test_correctness: PASS");
    return 0;
}
