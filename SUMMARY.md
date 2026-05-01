# Implementation Summary

## Public/Private Split

`src/art.h` is now public-facing only. It keeps `art_tree`, public API
functions, `art_stats`, and opaque declarations for `art_node` and `art_leaf`.

Internal structs moved to `src/art_internal.h`, including all node layouts,
leaf layout, pointer-tag macros, node type IDs, and small helper functions.

Because `art_leaf` is opaque, tests and callers now use:

```c
art_leaf_key(...)
art_leaf_key_len(...)
art_leaf_value(...)
```

This keeps benchmark and test code from depending on private struct layout.

## Node Representation Split

Node-specific mechanics were moved out of `art.c` into separate files (`nodeX.c`). Each node file implements only local representation operations:

```text
- find child
- add child when there is room
- remove child locally
- first child
- last child
- ordered child iteration
```

Node files do not decide growth or shrink targets.

## Node Dispatch Layer

`src/art_nodes.c` and `src/art_nodes.h` connect the main ART algorithm to the
node-specific files.

The main wrappers are:

```c
art_alloc_node(...)
art_find_child(...)
art_add_child(...)
art_remove_child(...)
art_first_child(...)
art_last_child(...)
art_for_each_child(...)
```

`art.c` calls these wrappers instead of switching directly on every node
representation.

`art_nodes.c` also owns generic grow/shrink orchestration:

```text
- detect full node
- ask the menu for the next node type
- allocate replacement
- copy header and children
- replace parent pointer
- free old node
```

Single-child path compression was factored into `art_compress_single_child(...)`
so both Node4 and Node2 can preserve ART compression after deletion.

## Node Menu Layer

`src/art_node_menu.h` defines compile-time node menus.

It provides:

```c
art_menu_min_type()
art_node_type_enabled(...)
art_node_capacity(...)
art_menu_next_type(...)
art_menu_shrink_type(...)
```

This decides which node types exist in a variant and when nodes grow or shrink.

`art.c` now uses `art_menu_min_type()` when creating new internal nodes, so
different variants can start with Node2, Node4, or Node256.

## New Node Types

Added `Node32KeyArray`:

```text
keys[32]
children[32]
```

It delays promotion from Node16 to Node48 for fanouts 17-32. This can save
memory, but lookup requires key comparison rather than direct indexing.

Added `Node64Indexed`:

```text
keys[256]
children[64]
```

It covers fanouts 49-64 and uses direct indexed lookup like Node48. This is the
current Node64 representation used by all active Node64 variants.

Added `Node2` and `Node5`:

```text
Node2: keys[2], children[2]
Node5: keys[5], children[5]
```

These support the paper-inspired six-node menu and test whether smaller
low-fanout nodes reduce memory.

## Compile-Time Variants

Each benchmark variant is a separate executable linked against a separate ART
library. This avoids runtime menu dispatch overhead.

Current variants:

```text
bench_original:
original4
Node4 -> Node16 -> Node48 -> Node256

bench_node32:
node32_keyarray
Node4 -> Node16 -> Node32KeyArray -> Node48 -> Node256

bench_node64:
node64
Node4 -> Node16 -> Node48 -> Node64Indexed -> Node256

bench_node32_node64:
node32_node64
Node4 -> Node16 -> Node32KeyArray -> Node48 -> Node64Indexed -> Node256

bench_paper6_indexed:
paper6_indexed
Node2 -> Node5 -> Node16 -> Node32KeyArray -> Node64Indexed -> Node256

bench_node256_only:
node256_only
Node256

bench_count1_node256:
count1_node256
Node256

bench_count2_16_256:
count2_node16_node256
Node16 -> Node256

bench_count3_4_16_256:
count3_node4_node16_node256
Node4 -> Node16 -> Node256

bench_count4_original4:
count4_original4
Node4 -> Node16 -> Node48 -> Node256

bench_count5_4_16_32_64_256:
count5_4_16_32_64_256
Node4 -> Node16 -> Node32KeyArray -> Node64Indexed -> Node256

bench_count6_4_16_32_48_64_256:
count6_4_16_32_48_64_256
Node4 -> Node16 -> Node32KeyArray -> Node48 -> Node64Indexed -> Node256

bench_count6_paper6:
count6_paper6
Node2 -> Node5 -> Node16 -> Node32KeyArray -> Node64Indexed -> Node256

bench_count7_2_5_16_32_48_64_256:
count7_2_5_16_32_48_64_256
Node2 -> Node5 -> Node16 -> Node32KeyArray -> Node48 -> Node64Indexed -> Node256
```

The menu is selected with CMake compile definitions such as:

```cmake
ART_MENU_NODE64_INDEXED
ART_MENU_NODE32_NODE64_INDEXED
ART_MENU_PAPER6_INDEXED
ART_MENU_NODE256_ONLY
ART_MENU_COUNT6_PAPER6
```

## Benchmark Pipeline

Added `bench/bench_original.c` as the shared benchmark executable source.

It supports:

```text
- dense_uint64
- sparse_uint64
- words_fixture
- uuid_fixture
- controlled_fanout
```

Each benchmark records:

```text
- insert
- successful lookup
- absent lookup
```

Successful lookup can use uniform shuffled access or Zipf-skewed access.

`scripts/run_benchmarks.py` runs sweeps over variants, workloads, sizes,
fanouts, groups, and Zipf parameters.

## Structural Stats

Added `art_collect_stats(...)` and extended benchmark CSV output with structural
metrics.

Stats include:

```text
- node counts by type
- bytes by node type
- internal node bytes
- leaf bytes
- total bytes
- bytes per key
- fanout histogram buckets
- average leaf depth
- maximum leaf depth
```

Memory uses actual `sizeof(...)` values, not paper-theoretical sizes.

## Correctness Tests

Added no-Check correctness targets for each variant. Coverage includes:

```text
- dense inserts/lookups
- sparse inserts/lookups
- missing keys
- overwrite behavior
- Node32 target fanout
- Node64 target fanout
- Node2 and Node5 target fanouts
- growth/shrink boundaries
- Node256-only empty-tree min/max behavior
- word fixture smoke test
```

Optional upstream Check tests are also built per variant when
`ART_BUILD_UPSTREAM_TESTS=ON`.

## Preserved Behavior

The public ART API and core semantics stay the same:

```text
- insert/search/delete behavior
- path compression
- lazy expansion
- leaf pointer tagging
- lexicographic iteration
- prefix iteration
- minimum/maximum APIs
- original4 baseline behavior
```

## Extension Instructions

To add a new node type:

```text
1. Add node type ID and struct in art_internal.h.
2. Add prototypes in art_nodes.h.
3. Implement local mechanics in nodeN.c.
4. Add dispatch cases in art_nodes.c.
5. Add capacity/grow/shrink rules in art_node_menu.h.
6. Add stats fields and CSV columns.
7. Add CMake library, benchmark, and correctness targets.
8. Add plotting labels if it appears in results.
```

To add a new menu:

```text
1. Add an ART_MENU_* compile definition.
2. Define enabled node types.
3. Define minimum node type.
4. Define grow targets.
5. Define shrink thresholds.
6. Add CMake targets.
7. Add tests that force the new fanout ranges.
```
