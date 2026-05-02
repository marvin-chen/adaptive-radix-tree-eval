#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "art.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
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

#ifndef ART_BENCH_VARIANT
#define ART_BENCH_VARIANT "original4"
#endif

typedef struct {
    unsigned char *data;
    int len;
} key_record;

typedef enum {
    WORKLOAD_DENSE_UINT64,
    WORKLOAD_SPARSE_UINT64,
    WORKLOAD_WORDS_FIXTURE,
    WORKLOAD_UUID_FIXTURE,
    WORKLOAD_CONTROLLED_FANOUT
} workload_kind;

typedef struct {
    workload_kind workload;
    uint64_t n;
    uint64_t seed;
    uint64_t groups;
    uint64_t fanout;
    uint64_t duplicate_keys;
    double zipf_s;
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

static uint64_t hash_key_bytes(const unsigned char *data, int len) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    hash ^= (uint64_t)len;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static int same_key(const key_record *a, const key_record *b) {
    return a->len == b->len && memcmp(a->data, b->data, (size_t)a->len) == 0;
}

static int deduplicate_keys(key_record *keys, uint64_t *count, uint64_t *duplicates) {
    uint64_t capacity = 1;
    while (capacity < (*count * 2u)) {
        if (capacity > (UINT64_MAX / 2u)) return -1;
        capacity *= 2u;
    }

    uint64_t *slots = (uint64_t*)malloc((size_t)capacity * sizeof(*slots));
    if (!slots) return -1;
    for (uint64_t i = 0; i < capacity; ++i) slots[i] = UINT64_MAX;

    uint64_t write = 0;
    uint64_t dup = 0;
    for (uint64_t read = 0; read < *count; ++read) {
        uint64_t slot = hash_key_bytes(keys[read].data, keys[read].len) & (capacity - 1u);
        int found = 0;
        while (slots[slot] != UINT64_MAX) {
            if (same_key(&keys[slots[slot]], &keys[read])) {
                found = 1;
                break;
            }
            slot = (slot + 1u) & (capacity - 1u);
        }

        if (found) {
            free(keys[read].data);
            dup++;
        } else {
            if (write != read) keys[write] = keys[read];
            slots[slot] = write++;
        }
    }

    free(slots);
    *count = write;
    if (duplicates) *duplicates = dup;
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

static int make_late_absent_keys(key_record **out, const key_record *keys, uint64_t n) {
    key_record *absent = (key_record*)calloc((size_t)n, sizeof(*absent));
    if (!absent) return -1;

    for (uint64_t i = 0; i < n; ++i) {
        int len = keys[i].len + 1;
        absent[i].data = (unsigned char*)malloc((size_t)len);
        if (!absent[i].data) {
            free_keys(absent, i);
            return -1;
        }
        memcpy(absent[i].data, keys[i].data, (size_t)keys[i].len);
        absent[i].data[len - 1] = 0xffu;
        absent[i].len = len;
    }

    *out = absent;
    return 0;
}

static int controlled_fanout_count(uint64_t groups, uint64_t fanout, uint64_t *out_n) {
    if (groups == 0 || fanout == 0 || fanout > 255) return -1;
    if (groups > (uint64_t)UINT32_MAX + UINT64_C(1)) return -1;
    if (groups > UINT64_MAX / fanout) return -1;
    *out_n = groups * fanout;
    return 0;
}

static int make_controlled_fanout_keys(key_record **out, uint64_t groups,
                                       uint64_t fanout, int absent) {
    uint64_t n = 0;
    if (controlled_fanout_count(groups, fanout, &n) != 0) return -1;

    key_record *keys = (key_record*)calloc((size_t)n, sizeof(*keys));
    if (!keys) return -1;

    uint64_t idx = 0;
    for (uint64_t group = 0; group < groups; ++group) {
        for (uint64_t child = 0; child < fanout; ++child) {
            unsigned char key[6];
            key[0] = (unsigned char)((group >> 24) & 0xffu);
            key[1] = (unsigned char)((group >> 16) & 0xffu);
            key[2] = (unsigned char)((group >> 8) & 0xffu);
            key[3] = (unsigned char)(group & 0xffu);
            key[4] = (unsigned char)child;
            key[5] = absent ? 1u : 0u;
            if (copy_key(&keys[idx], key, sizeof(key)) != 0) {
                free_keys(keys, idx);
                return -1;
            }
            idx++;
        }
    }

    *out = keys;
    return 0;
}

static int load_workload(workload_kind workload, uint64_t requested_n, uint64_t seed,
                         uint64_t groups, uint64_t fanout, key_record **keys,
                         key_record **absent_early, key_record **absent_late,
                         uint64_t *actual_n, uint64_t *duplicate_keys) {
    int rc = -1;
    *duplicate_keys = 0;

    switch (workload) {
        case WORKLOAD_DENSE_UINT64:
            *actual_n = requested_n;
            rc = make_uint64_keys(keys, requested_n, seed, 0, 0) ||
                 make_uint64_keys(absent_early, requested_n, seed, 0, 1);
            break;
        case WORKLOAD_SPARSE_UINT64:
            *actual_n = requested_n;
            rc = make_uint64_keys(keys, requested_n, seed, 1, 0) ||
                 make_uint64_keys(absent_early, requested_n, seed ^ UINT64_C(0xd1b54a32d192ed03), 1, 1);
            break;
        case WORKLOAD_WORDS_FIXTURE:
            if (load_fixture_keys("tests/words.txt", requested_n, keys, actual_n) != 0) return -1;
            if (deduplicate_keys(*keys, actual_n, duplicate_keys) != 0) return -1;
            rc = make_fixture_absent_keys(absent_early, *actual_n);
            break;
        case WORKLOAD_UUID_FIXTURE:
            if (load_fixture_keys("tests/uuid.txt", requested_n, keys, actual_n) != 0) return -1;
            if (deduplicate_keys(*keys, actual_n, duplicate_keys) != 0) return -1;
            rc = make_fixture_absent_keys(absent_early, *actual_n);
            break;
        case WORKLOAD_CONTROLLED_FANOUT:
            if (controlled_fanout_count(groups, fanout, actual_n) != 0) return -1;
            rc = make_controlled_fanout_keys(keys, groups, fanout, 0) ||
                 make_controlled_fanout_keys(absent_early, groups, fanout, 1);
            break;
        default:
            return -1;
    }

    if (rc != 0) return -1;
    return make_late_absent_keys(absent_late, *keys, *actual_n);
}

static const char *workload_name(workload_kind workload) {
    switch (workload) {
        case WORKLOAD_DENSE_UINT64: return "dense_uint64";
        case WORKLOAD_SPARSE_UINT64: return "sparse_uint64";
        case WORKLOAD_WORDS_FIXTURE: return "words_fixture";
        case WORKLOAD_UUID_FIXTURE: return "uuid_fixture";
        case WORKLOAD_CONTROLLED_FANOUT: return "controlled_fanout";
        default: return "unknown";
    }
}

static int parse_workload(const char *value, workload_kind *out) {
    if (strcmp(value, "dense_uint64") == 0) *out = WORKLOAD_DENSE_UINT64;
    else if (strcmp(value, "sparse_uint64") == 0) *out = WORKLOAD_SPARSE_UINT64;
    else if (strcmp(value, "words_fixture") == 0) *out = WORKLOAD_WORDS_FIXTURE;
    else if (strcmp(value, "uuid_fixture") == 0) *out = WORKLOAD_UUID_FIXTURE;
    else if (strcmp(value, "controlled_fanout") == 0) *out = WORKLOAD_CONTROLLED_FANOUT;
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

static double random_unit(uint64_t *state) {
    return (double)(splitmix64(state) >> 11) / 9007199254740992.0;
}

static uint64_t lower_bound_double(const double *values, uint64_t n, double needle) {
    uint64_t lo = 0;
    uint64_t hi = n;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (values[mid] < needle) lo = mid + 1;
        else hi = mid;
    }
    return lo == n ? n - 1 : lo;
}

static int make_zipf_lookup_order(uint64_t *order, uint64_t n, uint64_t seed, double s) {
    uint64_t *rank_to_index = (uint64_t*)malloc((size_t)n * sizeof(*rank_to_index));
    double *cdf = (double*)malloc((size_t)n * sizeof(*cdf));
    if (!rank_to_index || !cdf) {
        free(rank_to_index);
        free(cdf);
        return -1;
    }

    shuffle_order(rank_to_index, n, seed ^ UINT64_C(0xa0761d6478bd642f));

    double total = 0.0;
    for (uint64_t i = 0; i < n; ++i) {
        total += 1.0 / pow((double)i + 1.0, s);
        cdf[i] = total;
    }
    for (uint64_t i = 0; i < n; ++i) {
        cdf[i] /= total;
    }

    uint64_t state = seed ^ UINT64_C(0xe7037ed1a0b428db);
    for (uint64_t i = 0; i < n; ++i) {
        uint64_t rank = lower_bound_double(cdf, n, random_unit(&state));
        order[i] = rank_to_index[rank];
    }

    free(rank_to_index);
    free(cdf);
    return 0;
}

static int output_needs_header(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    int ch = fgetc(f);
    fclose(f);
    return ch == EOF;
}

static uint64_t fanout_range_sum(const art_stats *stats, int first, int last) {
    uint64_t total = 0;
    for (int i = first; i <= last; ++i) {
        total += stats->fanout_hist[i];
    }
    return total;
}

static int write_row(FILE *out, const bench_config *cfg, uint64_t actual_n, int run,
                     const char *operation, double seconds, uint64_t ops,
                     const art_stats *stats, uint64_t duplicate_keys) {
    double mops = seconds > 0.0 ? ((double)ops / seconds) / 1000000.0 : 0.0;
    double bytes_per_key = stats->keys
        ? (double)stats->total_bytes / (double)stats->keys
        : 0.0;
    double avg_leaf_depth = stats->leaf_count
        ? (double)stats->leaf_depth_sum / (double)stats->leaf_count
        : 0.0;
    double row_zipf_s = strcmp(operation, "lookup_success") == 0 ? cfg->zipf_s : 0.0;

    return fprintf(out,
                   "%s,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.6g,%d,%s,%.9f,%" PRIu64 ",%.6f,%" PRIu64
                   ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                   ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                   ",%.3f,%.3f,%" PRIu32
                   ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                   ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                   ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                   ",%" PRIu64 ",%" PRIu64 "\n",
                   ART_BENCH_VARIANT, workload_name(cfg->workload), actual_n, cfg->seed,
                   cfg->fanout, cfg->groups, row_zipf_s, run,
                   operation, seconds, ops, mops, actual_n,
                   stats->node4_count, stats->node16_count, stats->node32_count,
                   stats->node48_count, stats->node256_count, stats->internal_node_count,
                   stats->leaf_count, stats->internal_node_bytes, stats->leaf_bytes,
                   stats->total_bytes, bytes_per_key, avg_leaf_depth, stats->max_depth,
                   fanout_range_sum(stats, 0, 4),
                   fanout_range_sum(stats, 5, 16),
                   fanout_range_sum(stats, 17, 32),
                   fanout_range_sum(stats, 33, 48),
                   fanout_range_sum(stats, 49, 64),
                   fanout_range_sum(stats, 65, 256),
                   stats->node64_count, stats->node4_bytes, stats->node16_bytes,
                   stats->node32_bytes, stats->node48_bytes, stats->node64_bytes,
                   stats->node256_bytes, stats->node2_count, stats->node5_count,
                   stats->node2_bytes, stats->node5_bytes,
                   fanout_range_sum(stats, 1, 2),
                   fanout_range_sum(stats, 3, 5),
                   fanout_range_sum(stats, 6, 16),
                   stats->keys, duplicate_keys) < 0 ? -1 : 0;
}

static int run_one(const bench_config *cfg, FILE *out, key_record *keys,
                   key_record *absent_early, key_record *absent_late,
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
    if (cfg->zipf_s > 0.0) {
        if (make_zipf_lookup_order(lookup_order, actual_n,
                cfg->seed ^ ((uint64_t)run * UINT64_C(11400714819323198485)),
                cfg->zipf_s) != 0) {
            free(values);
            free(insert_order);
            free(lookup_order);
            return -1;
        }
    } else {
        shuffle_order(lookup_order, actual_n,
                cfg->seed ^ ((uint64_t)run * UINT64_C(11400714819323198485)));
    }

    art_tree tree;
    art_tree_init(&tree);
    int rc = -1;

    double start = now_seconds();
    uint64_t insert_duplicates = 0;
    for (uint64_t i = 0; i < actual_n; ++i) {
        uint64_t idx = insert_order[i];
        if (art_insert(&tree, keys[idx].data, keys[idx].len, &values[idx])) {
            insert_duplicates++;
        }
    }
    double insert_seconds = now_seconds() - start;
    uint64_t duplicate_keys = cfg->duplicate_keys + insert_duplicates;

    uintptr_t checksum = 0;
    start = now_seconds();
    for (uint64_t i = 0; i < actual_n; ++i) {
        uint64_t idx = lookup_order[i];
        void *value = art_search(&tree, keys[idx].data, keys[idx].len);
        if ((insert_duplicates && !value) || (!insert_duplicates && value != &values[idx])) {
            fprintf(stderr, "successful lookup returned wrong value at index %" PRIu64 "\n", idx);
            goto cleanup;
        }
        checksum ^= (uintptr_t)value;
    }
    double lookup_seconds = now_seconds() - start;
    bench_sink ^= checksum;

    checksum = 0;
    start = now_seconds();
    for (uint64_t i = 0; i < actual_n; ++i) {
        void *value = art_search(&tree, absent_early[i].data, absent_early[i].len);
        if (value) {
            fprintf(stderr, "early absent lookup unexpectedly found a value at index %" PRIu64 "\n", i);
            goto cleanup;
        }
        checksum ^= (uintptr_t)value;
    }
    double absent_early_seconds = now_seconds() - start;
    bench_sink ^= checksum;

    checksum = 0;
    start = now_seconds();
    for (uint64_t i = 0; i < actual_n; ++i) {
        void *value = art_search(&tree, absent_late[i].data, absent_late[i].len);
        if (value) {
            fprintf(stderr, "late absent lookup unexpectedly found a value at index %" PRIu64 "\n", i);
            goto cleanup;
        }
        checksum ^= (uintptr_t)value;
    }
    double absent_late_seconds = now_seconds() - start;
    bench_sink ^= checksum;

    art_stats stats;
    if (art_collect_stats(&tree, &stats) != 0) goto cleanup;
    if (write_row(out, cfg, actual_n, run, "insert", insert_seconds, actual_n, &stats, duplicate_keys) != 0) goto cleanup;
    if (write_row(out, cfg, actual_n, run, "lookup_success", lookup_seconds, actual_n, &stats, duplicate_keys) != 0) goto cleanup;
    if (write_row(out, cfg, actual_n, run, "lookup_absent_early", absent_early_seconds, actual_n, &stats, duplicate_keys) != 0) goto cleanup;
    if (write_row(out, cfg, actual_n, run, "lookup_absent_late", absent_late_seconds, actual_n, &stats, duplicate_keys) != 0) goto cleanup;

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
            "usage: %s --workload dense_uint64|sparse_uint64|words_fixture|uuid_fixture|controlled_fanout "
            "--seed <uint64> --runs <count> --out <csv path> [--zipf <s>] "
            "[--n <count> | --groups <count> --fanout <count>]\n",
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

static int parse_double(const char *text, double *out) {
    char *end = NULL;
    errno = 0;
    double value = strtod(text, &end);
    if (errno || !end || *end) return -1;
    *out = value;
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
        } else if (strcmp(argv[i], "--groups") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &cfg->groups) != 0) return -1;
        } else if (strcmp(argv[i], "--fanout") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &cfg->fanout) != 0) return -1;
        } else if (strcmp(argv[i], "--zipf") == 0 && i + 1 < argc) {
            if (parse_double(argv[++i], &cfg->zipf_s) != 0 || cfg->zipf_s < 0.0) return -1;
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

    if (cfg->workload == WORKLOAD_CONTROLLED_FANOUT) {
        uint64_t actual_n = 0;
        return cfg->out_path != NULL &&
               controlled_fanout_count(cfg->groups, cfg->fanout, &actual_n) == 0 ? 0 : -1;
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
    key_record *absent_early = NULL;
    key_record *absent_late = NULL;
    uint64_t actual_n = 0;
    if (load_workload(cfg.workload, cfg.n, cfg.seed, cfg.groups, cfg.fanout,
            &keys, &absent_early, &absent_late, &actual_n, &cfg.duplicate_keys) != 0) {
        fprintf(stderr, "failed to load workload %s\n", workload_name(cfg.workload));
        free_keys(keys, actual_n);
        free_keys(absent_early, actual_n);
        free_keys(absent_late, actual_n);
        return 1;
    }

    int needs_header = output_needs_header(cfg.out_path);
    FILE *out = fopen(cfg.out_path, "a");
    if (!out) {
        fprintf(stderr, "failed to open output %s: %s\n", cfg.out_path, strerror(errno));
        free_keys(keys, actual_n);
        free_keys(absent_early, actual_n);
        free_keys(absent_late, actual_n);
        return 1;
    }
    if (needs_header) {
        fprintf(out,
                "variant,workload,n,seed,fanout_target,groups,zipf_s,run,operation,seconds,ops,mops_per_sec,keys,"
                "node4,node16,node32,node48,node256,internal_nodes,leaves,"
                "internal_node_bytes,leaf_bytes,total_bytes,bytes_per_key,"
                "avg_leaf_depth,max_leaf_depth,fanout_0_4,fanout_5_16,"
                "fanout_17_32,fanout_33_48,fanout_49_64,fanout_65_256,"
                "node64,node4_bytes,node16_bytes,node32_bytes,node48_bytes,"
                "node64_bytes,node256_bytes,node2,node5,node2_bytes,node5_bytes,"
                "fanout_1_2,fanout_3_5,fanout_6_16,unique_keys,duplicate_keys\n");
    }

    int rc = 0;
    for (int run = 0; run < cfg.runs; ++run) {
        if (run_one(&cfg, out, keys, absent_early, absent_late, actual_n, run) != 0) {
            rc = 1;
            break;
        }
    }

    fclose(out);
    free_keys(keys, actual_n);
    free_keys(absent_early, actual_n);
    free_keys(absent_late, actual_n);
    return rc;
}
