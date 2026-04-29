#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "art.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#ifndef ART_PROJECT_SOURCE_DIR
#define ART_PROJECT_SOURCE_DIR "."
#endif

typedef struct {
    unsigned char *data;
    int len;
} key_record;

typedef enum {
    WORKLOAD_DENSE_UINT64,
    WORKLOAD_SPARSE_UINT64,
    WORKLOAD_WORDS_FIXTURE,
    WORKLOAD_UUID_FIXTURE
} workload_kind;

typedef struct {
    workload_kind workload;
    uint64_t n;
    uint64_t seed;
    int runs;
    const char *out_path;
} bench_config;

static volatile uintptr_t bench_sink;

static double now_seconds(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart) {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#endif
}

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

static int copy_key(key_record *dst, const unsigned char *src, int len) {
    dst->data = (unsigned char*)malloc((size_t)len);
    if (!dst->data) return -1;
    memcpy(dst->data, src, (size_t)len);
    dst->len = len;
    return 0;
}

static void free_keys(key_record *keys, uint64_t count) {
    if (!keys) return;
    for (uint64_t i = 0; i < count; ++i) {
        free(keys[i].data);
    }
    free(keys);
}

static int make_uint64_keys(key_record **out, uint64_t n, uint64_t seed, int sparse, int absent) {
    key_record *keys = (key_record*)calloc((size_t)n, sizeof(*keys));
    if (!keys) return -1;

    for (uint64_t i = 0; i < n; ++i) {
        unsigned char bytes[8];
        uint64_t value;
        if (sparse) {
            uint64_t state = seed + i;
            value = splitmix64(&state) << 1;
            if (absent) value |= 1u;
        } else {
            value = absent ? (n + i + 1) : i;
        }
        encode_u64_be(value, bytes);
        if (copy_key(&keys[i], bytes, 8) != 0) {
            free_keys(keys, n);
            return -1;
        }
    }

    *out = keys;
    return 0;
}

static int append_fixture_absent_key(key_record *dst, uint64_t index) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "\xff""missing-%" PRIu64, index);
    if (len <= 0 || len >= (int)sizeof(buf)) return -1;
    return copy_key(dst, (const unsigned char*)buf, len + 1);
}

static int load_fixture_keys(const char *relative_path, uint64_t cap, key_record **out, uint64_t *out_count) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", ART_PROJECT_SOURCE_DIR, relative_path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open fixture %s: %s\n", path, strerror(errno));
        return -1;
    }

    uint64_t capacity = cap ? cap : 1024;
    key_record *keys = (key_record*)calloc((size_t)capacity, sizeof(*keys));
    if (!keys) {
        fclose(f);
        return -1;
    }

    char buf[4096];
    uint64_t count = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
            buf[--len] = '\0';
        }
        if (len == 0) continue;

        if (count == capacity) {
            if (cap) break;
            capacity *= 2;
            key_record *next = (key_record*)realloc(keys, (size_t)capacity * sizeof(*keys));
            if (!next) {
                fclose(f);
                free_keys(keys, count);
                return -1;
            }
            memset(next + count, 0, (size_t)(capacity - count) * sizeof(*next));
            keys = next;
        }

        if (copy_key(&keys[count], (const unsigned char*)buf, (int)len + 1) != 0) {
            fclose(f);
            free_keys(keys, count);
            return -1;
        }
        count++;
        if (cap && count >= cap) break;
    }

    fclose(f);
    *out = keys;
    *out_count = count;
    return count ? 0 : -1;
}

static int make_fixture_absent_keys(key_record **out, uint64_t n) {
    key_record *keys = (key_record*)calloc((size_t)n, sizeof(*keys));
    if (!keys) return -1;
    for (uint64_t i = 0; i < n; ++i) {
        if (append_fixture_absent_key(&keys[i], i) != 0) {
            free_keys(keys, n);
            return -1;
        }
    }
    *out = keys;
    return 0;
}

static int load_workload(workload_kind workload, uint64_t requested_n, uint64_t seed,
                         key_record **keys, key_record **absent, uint64_t *actual_n) {
    switch (workload) {
        case WORKLOAD_DENSE_UINT64:
            *actual_n = requested_n;
            return make_uint64_keys(keys, requested_n, seed, 0, 0) ||
                   make_uint64_keys(absent, requested_n, seed, 0, 1);
        case WORKLOAD_SPARSE_UINT64:
            *actual_n = requested_n;
            return make_uint64_keys(keys, requested_n, seed, 1, 0) ||
                   make_uint64_keys(absent, requested_n, seed ^ UINT64_C(0xd1b54a32d192ed03), 1, 1);
        case WORKLOAD_WORDS_FIXTURE:
            if (load_fixture_keys("tests/words.txt", requested_n, keys, actual_n) != 0) return -1;
            return make_fixture_absent_keys(absent, *actual_n);
        case WORKLOAD_UUID_FIXTURE:
            if (load_fixture_keys("tests/uuid.txt", requested_n, keys, actual_n) != 0) return -1;
            return make_fixture_absent_keys(absent, *actual_n);
        default:
            return -1;
    }
}

static const char *workload_name(workload_kind workload) {
    switch (workload) {
        case WORKLOAD_DENSE_UINT64: return "dense_uint64";
        case WORKLOAD_SPARSE_UINT64: return "sparse_uint64";
        case WORKLOAD_WORDS_FIXTURE: return "words_fixture";
        case WORKLOAD_UUID_FIXTURE: return "uuid_fixture";
        default: return "unknown";
    }
}

static int parse_workload(const char *value, workload_kind *out) {
    if (strcmp(value, "dense_uint64") == 0) *out = WORKLOAD_DENSE_UINT64;
    else if (strcmp(value, "sparse_uint64") == 0) *out = WORKLOAD_SPARSE_UINT64;
    else if (strcmp(value, "words_fixture") == 0) *out = WORKLOAD_WORDS_FIXTURE;
    else if (strcmp(value, "uuid_fixture") == 0) *out = WORKLOAD_UUID_FIXTURE;
    else return -1;
    return 0;
}

static void shuffle_order(uint64_t *order, uint64_t n, uint64_t seed) {
    for (uint64_t i = 0; i < n; ++i) order[i] = i;
    if (n < 2) return;
    uint64_t state = seed;
    for (uint64_t i = n - 1; i > 0; --i) {
        uint64_t j = splitmix64(&state) % (i + 1);
        uint64_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
}

static int output_needs_header(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    int ch = fgetc(f);
    fclose(f);
    return ch == EOF;
}

static int write_row(FILE *out, const bench_config *cfg, uint64_t actual_n, int run,
                     const char *operation, double seconds, uint64_t ops) {
    double mops = seconds > 0.0 ? ((double)ops / seconds) / 1000000.0 : 0.0;
    return fprintf(out,
                   "original4,%s,%" PRIu64 ",%" PRIu64 ",%d,%s,%.9f,%" PRIu64 ",%.6f,%" PRIu64 "\n",
                   workload_name(cfg->workload), actual_n, cfg->seed, run,
                   operation, seconds, ops, mops, actual_n) < 0 ? -1 : 0;
}

static int run_one(const bench_config *cfg, FILE *out, key_record *keys, key_record *absent,
                   uint64_t actual_n, int run) {
    uintptr_t *values = (uintptr_t*)calloc((size_t)actual_n, sizeof(*values));
    uint64_t *insert_order = (uint64_t*)malloc((size_t)actual_n * sizeof(*insert_order));
    uint64_t *lookup_order = (uint64_t*)malloc((size_t)actual_n * sizeof(*lookup_order));
    if (!values || !insert_order || !lookup_order) {
        free(values);
        free(insert_order);
        free(lookup_order);
        return -1;
    }

    for (uint64_t i = 0; i < actual_n; ++i) values[i] = i + 1;
    shuffle_order(insert_order, actual_n, cfg->seed + (uint64_t)run * UINT64_C(1315423911));
    shuffle_order(lookup_order, actual_n, cfg->seed ^ ((uint64_t)run * UINT64_C(11400714819323198485)));

    art_tree tree;
    art_tree_init(&tree);
    int rc = -1;

    double start = now_seconds();
    for (uint64_t i = 0; i < actual_n; ++i) {
        uint64_t idx = insert_order[i];
        art_insert(&tree, keys[idx].data, keys[idx].len, &values[idx]);
    }
    double seconds = now_seconds() - start;
    if (write_row(out, cfg, actual_n, run, "insert", seconds, actual_n) != 0) goto cleanup;

    uintptr_t checksum = 0;
    start = now_seconds();
    for (uint64_t i = 0; i < actual_n; ++i) {
        uint64_t idx = lookup_order[i];
        void *value = art_search(&tree, keys[idx].data, keys[idx].len);
        if (!value) {
            fprintf(stderr, "successful lookup failed at index %" PRIu64 "\n", idx);
            goto cleanup;
        }
        checksum ^= (uintptr_t)value;
    }
    seconds = now_seconds() - start;
    bench_sink ^= checksum;
    if (write_row(out, cfg, actual_n, run, "lookup_success", seconds, actual_n) != 0) goto cleanup;

    checksum = 0;
    start = now_seconds();
    for (uint64_t i = 0; i < actual_n; ++i) {
        void *value = art_search(&tree, absent[i].data, absent[i].len);
        if (value) {
            fprintf(stderr, "absent lookup unexpectedly found a value at index %" PRIu64 "\n", i);
            goto cleanup;
        }
        checksum ^= (uintptr_t)value;
    }
    seconds = now_seconds() - start;
    bench_sink ^= checksum;
    if (write_row(out, cfg, actual_n, run, "lookup_absent", seconds, actual_n) != 0) goto cleanup;

    rc = 0;

cleanup:
    art_tree_destroy(&tree);
    free(values);
    free(insert_order);
    free(lookup_order);
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s --workload dense_uint64|sparse_uint64|words_fixture|uuid_fixture "
            "--n <count> --seed <uint64> --runs <count> --out <csv path>\n",
            argv0);
}

static int parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || !end || *end) return -1;
    *out = (uint64_t)value;
    return 0;
}

static int parse_args(int argc, char **argv, bench_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->runs = 1;
    cfg->seed = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--workload") == 0 && i + 1 < argc) {
            if (parse_workload(argv[++i], &cfg->workload) != 0) return -1;
        } else if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &cfg->n) != 0) return -1;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &cfg->seed) != 0) return -1;
        } else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            uint64_t runs = 0;
            if (parse_u64(argv[++i], &runs) != 0 || runs == 0 || runs > 1000000) return -1;
            cfg->runs = (int)runs;
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            cfg->out_path = argv[++i];
        } else {
            return -1;
        }
    }

    return cfg->n > 0 && cfg->out_path != NULL ? 0 : -1;
}

int main(int argc, char **argv) {
    bench_config cfg;
    if (parse_args(argc, argv, &cfg) != 0) {
        usage(argv[0]);
        return 2;
    }

    key_record *keys = NULL;
    key_record *absent = NULL;
    uint64_t actual_n = 0;
    if (load_workload(cfg.workload, cfg.n, cfg.seed, &keys, &absent, &actual_n) != 0) {
        fprintf(stderr, "failed to load workload %s\n", workload_name(cfg.workload));
        free_keys(keys, actual_n);
        free_keys(absent, actual_n);
        return 1;
    }

    int needs_header = output_needs_header(cfg.out_path);
    FILE *out = fopen(cfg.out_path, "a");
    if (!out) {
        fprintf(stderr, "failed to open output %s: %s\n", cfg.out_path, strerror(errno));
        free_keys(keys, actual_n);
        free_keys(absent, actual_n);
        return 1;
    }
    if (needs_header) {
        fprintf(out, "variant,workload,n,seed,run,operation,seconds,ops,mops_per_sec,keys\n");
    }

    int rc = 0;
    for (int run = 0; run < cfg.runs; ++run) {
        if (run_one(&cfg, out, keys, absent, actual_n, run) != 0) {
            rc = 1;
            break;
        }
    }

    fclose(out);
    free_keys(keys, actual_n);
    free_keys(absent, actual_n);
    return rc;
}
