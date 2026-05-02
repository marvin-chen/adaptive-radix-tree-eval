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

typedef enum {
    SCAN_MODE_BOTH,
    SCAN_MODE_FULL,
    SCAN_MODE_PREFIX
} scan_mode;

typedef struct {
    workload_kind workload;
    uint64_t n;
    uint64_t seed;
    uint64_t groups;
    uint64_t fanout;
    uint64_t scan_passes;
    uint64_t prefix_samples;
    int prefix_len;
    int runs;
    scan_mode mode;
    const char *out_path;
} bench_config;

typedef struct {
    uint64_t visited;
    uintptr_t checksum;
} scan_ctx;

typedef struct {
    unsigned char *previous;
    uint32_t previous_len;
    uint64_t visited;
    const unsigned char *prefix;
    int prefix_len;
    int have_previous;
} order_check_ctx;

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

static int make_uint64_keys(key_record **out, uint64_t n, uint64_t seed, int sparse) {
    key_record *keys = (key_record*)calloc((size_t)n, sizeof(*keys));
    if (!keys) return -1;

    for (uint64_t i = 0; i < n; ++i) {
        unsigned char bytes[8];
        uint64_t value;
        if (sparse) {
            uint64_t state = seed + i;
            value = splitmix64(&state) << 1;
        } else {
            value = i;
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

static int load_fixture_keys(const char *relative_path, uint64_t cap,
                             key_record **out, uint64_t *out_count) {
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

static int controlled_fanout_count(uint64_t groups, uint64_t fanout, uint64_t *out_n) {
    if (groups == 0 || fanout == 0 || fanout > 255) return -1;
    if (groups > (uint64_t)UINT32_MAX + UINT64_C(1)) return -1;
    if (groups > UINT64_MAX / fanout) return -1;
    *out_n = groups * fanout;
    return 0;
}

static int make_controlled_fanout_keys(key_record **out, uint64_t groups,
                                       uint64_t fanout) {
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
            key[5] = 0;
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
                         uint64_t *actual_n) {
    switch (workload) {
        case WORKLOAD_DENSE_UINT64:
            *actual_n = requested_n;
            return make_uint64_keys(keys, requested_n, seed, 0);
        case WORKLOAD_SPARSE_UINT64:
            *actual_n = requested_n;
            return make_uint64_keys(keys, requested_n, seed, 1);
        case WORKLOAD_WORDS_FIXTURE:
            return load_fixture_keys("tests/words.txt", requested_n, keys, actual_n);
        case WORKLOAD_UUID_FIXTURE:
            return load_fixture_keys("tests/uuid.txt", requested_n, keys, actual_n);
        case WORKLOAD_CONTROLLED_FANOUT:
            if (controlled_fanout_count(groups, fanout, actual_n) != 0) return -1;
            return make_controlled_fanout_keys(keys, groups, fanout);
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

static int output_needs_header(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    int ch = fgetc(f);
    fclose(f);
    return ch == EOF;
}

static int scan_callback(void *data, const unsigned char *key,
                         uint32_t key_len, void *value) {
    scan_ctx *ctx = (scan_ctx*)data;
    ctx->visited++;
    ctx->checksum ^= (uintptr_t)value;
    ctx->checksum ^= ((uintptr_t)key_len << 17);
    if (key_len) ctx->checksum ^= key[0];
    return 0;
}

static int sample_prefix(key_record *dst, const key_record *keys, uint64_t key_count,
                         int prefix_len, uint64_t *state);

static int compare_keys(const unsigned char *a, uint32_t a_len,
                        const unsigned char *b, uint32_t b_len) {
    uint32_t min_len = a_len < b_len ? a_len : b_len;
    int cmp = min_len ? memcmp(a, b, min_len) : 0;
    if (cmp) return cmp;
    if (a_len < b_len) return -1;
    if (a_len > b_len) return 1;
    return 0;
}

static int has_prefix(const unsigned char *key, uint32_t key_len,
                      const unsigned char *prefix, int prefix_len) {
    return prefix_len <= (int)key_len &&
           memcmp(key, prefix, (size_t)prefix_len) == 0;
}

static int order_check_callback(void *data, const unsigned char *key,
                                uint32_t key_len, void *value) {
    order_check_ctx *ctx = (order_check_ctx*)data;
    (void)value;

    if (ctx->prefix_len > 0 && !has_prefix(key, key_len, ctx->prefix, ctx->prefix_len)) {
        fprintf(stderr, "prefix iteration returned a key outside the requested prefix\n");
        return -1;
    }

    if (ctx->have_previous &&
            compare_keys(ctx->previous, ctx->previous_len, key, key_len) > 0) {
        fprintf(stderr, "iteration returned keys out of lexicographic order\n");
        return -1;
    }

    unsigned char *copy = NULL;
    if (key_len > 0) {
        copy = (unsigned char*)malloc(key_len);
        if (!copy) return -1;
        memcpy(copy, key, key_len);
    }
    free(ctx->previous);
    ctx->previous = copy;
    ctx->previous_len = key_len;
    ctx->have_previous = 1;
    ctx->visited++;
    return 0;
}

static int validate_full_iteration(art_tree *tree, uint64_t expected) {
    order_check_ctx ctx = {0};
    int rc = art_iter(tree, order_check_callback, &ctx);
    if (rc == 0 && ctx.visited != expected) {
        fprintf(stderr, "full iteration validation visited %" PRIu64 ", expected %" PRIu64 "\n",
                ctx.visited, expected);
        rc = -1;
    }
    free(ctx.previous);
    return rc;
}

static int validate_prefix_scans(const bench_config *cfg, art_tree *tree,
                                 const key_record *keys, uint64_t actual_n, int run) {
    uint64_t state = cfg->seed ^ ((uint64_t)run * UINT64_C(11400714819323198485));
    for (uint64_t i = 0; i < cfg->prefix_samples; ++i) {
        key_record prefix = {0, 0};
        order_check_ctx ctx = {0};
        if (sample_prefix(&prefix, keys, actual_n, cfg->prefix_len, &state) != 0) {
            return -1;
        }

        ctx.prefix = prefix.data;
        ctx.prefix_len = prefix.len;
        int rc = art_iter_prefix(tree, prefix.data, prefix.len, order_check_callback, &ctx);
        if (rc == 0 && ctx.visited == 0) {
            fprintf(stderr, "prefix scan validation visited no entries at sample %" PRIu64 "\n", i);
            rc = -1;
        }
        free(ctx.previous);
        free(prefix.data);
        if (rc != 0) return -1;
    }
    return 0;
}

static int sample_prefix(key_record *dst, const key_record *keys, uint64_t key_count,
                         int prefix_len, uint64_t *state) {
    uint64_t start = splitmix64(state) % key_count;
    for (uint64_t offset = 0; offset < key_count; ++offset) {
        uint64_t idx = (start + offset) % key_count;
        if (keys[idx].len >= prefix_len) {
            return copy_key(dst, keys[idx].data, prefix_len);
        }
    }
    return -1;
}

static int write_row(FILE *out, const bench_config *cfg, uint64_t actual_n, int run,
                     const char *operation, int prefix_len, uint64_t scans,
                     double seconds, uint64_t visited, const art_stats *stats) {
    double mops = seconds > 0.0 ? ((double)visited / seconds) / 1000000.0 : 0.0;
    double scans_per_sec = seconds > 0.0 ? (double)scans / seconds : 0.0;
    double avg_entries = scans ? (double)visited / (double)scans : 0.0;
    double bytes_per_key = stats->keys
        ? (double)stats->total_bytes / (double)stats->keys
        : 0.0;
    double avg_leaf_depth = stats->leaf_count
        ? (double)stats->leaf_depth_sum / (double)stats->leaf_count
        : 0.0;

    return fprintf(out,
        "%s,%s,%" PRIu64 ",%" PRIu64 ",%d,%s,%.9f,%" PRIu64 ",%" PRIu64
        ",%.6f,%.6f,%.6f,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64
        ",%.6f,%" PRIu64 ",%" PRIu64 ",%.3f,%" PRIu32 "\n",
        ART_BENCH_VARIANT, workload_name(cfg->workload), actual_n, cfg->seed,
        run, operation, seconds, visited, scans, mops, scans_per_sec, avg_entries,
        prefix_len, cfg->fanout, cfg->groups, actual_n, bytes_per_key,
        stats->internal_node_bytes, stats->total_bytes, avg_leaf_depth,
        stats->max_depth) < 0 ? -1 : 0;
}

static int run_one(const bench_config *cfg, FILE *out, key_record *keys,
                   uint64_t actual_n, int run) {
    uintptr_t *values = (uintptr_t*)calloc((size_t)actual_n, sizeof(*values));
    uint64_t *insert_order = (uint64_t*)malloc((size_t)actual_n * sizeof(*insert_order));
    if (!values || !insert_order) {
        free(values);
        free(insert_order);
        return -1;
    }

    for (uint64_t i = 0; i < actual_n; ++i) values[i] = i + 1;
    shuffle_order(insert_order, actual_n, cfg->seed + (uint64_t)run * UINT64_C(1315423911));

    art_tree tree;
    art_tree_init(&tree);
    int rc = -1;

    for (uint64_t i = 0; i < actual_n; ++i) {
        uint64_t idx = insert_order[i];
        art_insert(&tree, keys[idx].data, keys[idx].len, &values[idx]);
    }

    int have_full = 0;
    int have_prefix = 0;
    double full_seconds = 0.0;
    double prefix_seconds = 0.0;
    uint64_t full_visited = 0;
    uint64_t prefix_visited = 0;

    if (cfg->mode == SCAN_MODE_BOTH || cfg->mode == SCAN_MODE_PREFIX) {
        scan_ctx ctx = {0, 0};
        uint64_t state = cfg->seed ^ ((uint64_t)run * UINT64_C(11400714819323198485));
        double start = now_seconds();
        for (uint64_t i = 0; i < cfg->prefix_samples; ++i) {
            key_record prefix = {0, 0};
            uint64_t before = ctx.visited;
            if (sample_prefix(&prefix, keys, actual_n, cfg->prefix_len, &state) != 0) goto cleanup;
            int iter_rc = art_iter_prefix(&tree, prefix.data, prefix.len, scan_callback, &ctx);
            free(prefix.data);
            if (iter_rc != 0) goto cleanup;
            if (ctx.visited == before) {
                fprintf(stderr, "prefix scan visited no entries at sample %" PRIu64 "\n", i);
                goto cleanup;
            }
        }
        prefix_seconds = now_seconds() - start;
        prefix_visited = ctx.visited;
        have_prefix = 1;
        bench_sink ^= ctx.checksum;
    }

    if (cfg->mode == SCAN_MODE_BOTH || cfg->mode == SCAN_MODE_FULL) {
        scan_ctx ctx = {0, 0};
        double start = now_seconds();
        for (uint64_t pass = 0; pass < cfg->scan_passes; ++pass) {
            if (art_iter(&tree, scan_callback, &ctx) != 0) goto cleanup;
        }
        full_seconds = now_seconds() - start;
        if (ctx.visited != actual_n * cfg->scan_passes) {
            fprintf(stderr, "full iteration visited %" PRIu64 ", expected %" PRIu64 "\n",
                    ctx.visited, actual_n * cfg->scan_passes);
            goto cleanup;
        }
        full_visited = ctx.visited;
        have_full = 1;
        bench_sink ^= ctx.checksum;
    }

    if (have_prefix && validate_prefix_scans(cfg, &tree, keys, actual_n, run) != 0) {
        goto cleanup;
    }
    if (have_full && validate_full_iteration(&tree, actual_n) != 0) {
        goto cleanup;
    }

    art_stats stats;
    if (art_collect_stats(&tree, &stats) != 0) goto cleanup;

    if (have_full && write_row(out, cfg, actual_n, run, "iter_full", 0, cfg->scan_passes,
            full_seconds, full_visited, &stats) != 0) goto cleanup;
    if (have_prefix && write_row(out, cfg, actual_n, run, "iter_prefix", cfg->prefix_len,
            cfg->prefix_samples, prefix_seconds, prefix_visited, &stats) != 0) goto cleanup;

    rc = 0;

cleanup:
    art_tree_destroy(&tree);
    free(values);
    free(insert_order);
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s --workload dense_uint64|sparse_uint64|words_fixture|uuid_fixture|controlled_fanout "
            "--seed <uint64> --runs <count> --out <csv path> "
            "[--mode full|prefix|both] [--prefix-len <bytes>] "
            "[--scan-passes <count>] [--prefix-samples <count>] "
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

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || !end || *end || value <= 0 || value > 1024) return -1;
    *out = (int)value;
    return 0;
}

static int parse_args(int argc, char **argv, bench_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->runs = 1;
    cfg->seed = 1;
    cfg->scan_passes = 3;
    cfg->prefix_samples = 512;
    cfg->prefix_len = 1;
    cfg->mode = SCAN_MODE_BOTH;

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
        } else if (strcmp(argv[i], "--prefix-len") == 0 && i + 1 < argc) {
            if (parse_int(argv[++i], &cfg->prefix_len) != 0) return -1;
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            if (strcmp(mode, "both") == 0) cfg->mode = SCAN_MODE_BOTH;
            else if (strcmp(mode, "full") == 0) cfg->mode = SCAN_MODE_FULL;
            else if (strcmp(mode, "prefix") == 0) cfg->mode = SCAN_MODE_PREFIX;
            else return -1;
        } else if (strcmp(argv[i], "--scan-passes") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &cfg->scan_passes) != 0 || cfg->scan_passes == 0) return -1;
        } else if (strcmp(argv[i], "--prefix-samples") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &cfg->prefix_samples) != 0 || cfg->prefix_samples == 0) return -1;
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
    uint64_t actual_n = 0;
    if (load_workload(cfg.workload, cfg.n, cfg.seed, cfg.groups, cfg.fanout,
            &keys, &actual_n) != 0) {
        fprintf(stderr, "failed to load workload %s\n", workload_name(cfg.workload));
        free_keys(keys, actual_n);
        return 1;
    }

    int needs_header = output_needs_header(cfg.out_path);
    FILE *out = fopen(cfg.out_path, "a");
    if (!out) {
        fprintf(stderr, "failed to open output %s: %s\n", cfg.out_path, strerror(errno));
        free_keys(keys, actual_n);
        return 1;
    }
    if (needs_header) {
        fprintf(out,
                "variant,workload,n,seed,run,operation,seconds,visited,scans,"
                "mops_per_sec,scans_per_sec,avg_entries_per_scan,prefix_len,"
                "fanout_target,groups,keys,bytes_per_key,internal_node_bytes,"
                "total_bytes,avg_leaf_depth,max_leaf_depth\n");
    }

    int rc = 0;
    for (int run = 0; run < cfg.runs; ++run) {
        if (run_one(&cfg, out, keys, actual_n, run) != 0) {
            rc = 1;
            break;
        }
    }

    fclose(out);
    free_keys(keys, actual_n);
    return rc;
}
