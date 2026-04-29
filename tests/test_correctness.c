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
    test_words_fixture_smoke();
    puts("test_correctness: PASS");
    return 0;
}
