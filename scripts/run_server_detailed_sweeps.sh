#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
RUN_ID="${RUN_ID:-server_detailed_$(date +%Y%m%d_%H%M%S)}"
OUT_ROOT="${OUT_ROOT:-output/${RUN_ID}}"

PYTHON="${PYTHON:-python3}"
RUN_CONFIGURE="${RUN_CONFIGURE:-1}"
RUN_BUILD="${RUN_BUILD:-1}"
RUN_TESTS="${RUN_TESTS:-1}"
BUILD_UPSTREAM_TESTS="${BUILD_UPSTREAM_TESTS:-OFF}"

RUNS="${RUNS:-10}"
ZIPF_RUNS="${ZIPF_RUNS:-10}"
SCAN_RUNS="${SCAN_RUNS:-10}"
SEED="${SEED:-1}"

REALISTIC_SIZES="${REALISTIC_SIZES:-4096,8192,16384,32768,65536,131072,262144,524288,1048576,2097152,4194304}"
CONTROLLED_FANOUTS="${CONTROLLED_FANOUTS:-1,2,3,4,5,6,7,8,10,12,15,16,17,18,24,31,32,33,34,37,40,47,48,49,50,56,63,64,65,66,80,96,112,128,160,192,224,255}"
CONTROLLED_GROUPS="${CONTROLLED_GROUPS:-100,1000,10000}"
ZIPF_SIZES="${ZIPF_SIZES:-65536,262144,1048576}"
ZIPFS="${ZIPFS:-0,0.25,0.5,0.75,1.0,1.25,1.5}"

SCAN_SIZES="${SCAN_SIZES:-4096,16384,65536,262144,1048576,4194304}"
SCAN_CONTROLLED_GROUPS="${SCAN_CONTROLLED_GROUPS:-1000,10000}"
SCAN_PASSES="${SCAN_PASSES:-3}"
PREFIX_SAMPLES="${PREFIX_SAMPLES:-512}"

mkdir -p "$OUT_ROOT/raw" "$OUT_ROOT/scan/raw" "$OUT_ROOT/logs"

bench_path() {
  printf "%s/%s" "$BUILD_DIR" "$1"
}

join_csv() {
  local IFS=,
  echo "$*"
}

# Default suite: every menu-count variant plus standalone Node32 and Node64.
# Set INCLUDE_MAIN_ALIASES=1 to also emit duplicate main labels such as
# original4, node32_node64, paper6_indexed, and node256_only.
BENCH_STEMS=(
  bench_count1_node256
  bench_count2_16_256
  bench_count3_4_16_256
  bench_count4_original4
  bench_node32
  bench_node64
  bench_count5_4_16_32_64_256
  bench_count6_4_16_32_48_64_256
  bench_count6_paper6
  bench_count7_2_5_16_32_48_64_256
)

SCAN_STEMS=(
  bench_scan_count1_node256
  bench_scan_count2_16_256
  bench_scan_count3_4_16_256
  bench_scan_count4_original4
  bench_scan_node32
  bench_scan_node64
  bench_scan_count5_4_16_32_64_256
  bench_scan_count6_4_16_32_48_64_256
  bench_scan_count6_paper6
  bench_scan_count7_2_5_16_32_48_64_256
)

if [[ "${INCLUDE_MAIN_ALIASES:-0}" == "1" ]]; then
  BENCH_STEMS+=(
    bench_original
    bench_node32_node64
    bench_paper6_indexed
    bench_node256_only
  )
  SCAN_STEMS+=(
    bench_scan_original
    bench_scan_node32_node64
    bench_scan_paper6_indexed
    bench_scan_node256_only
  )
fi

BENCH_PATHS=()
for stem in "${BENCH_STEMS[@]}"; do
  BENCH_PATHS+=("$(bench_path "$stem")")
done
BENCHES="$(join_csv "${BENCH_PATHS[@]}")"

SCAN_PATHS=()
for stem in "${SCAN_STEMS[@]}"; do
  SCAN_PATHS+=("$(bench_path "$stem")")
done
SCAN_BENCHES="$(join_csv "${SCAN_PATHS[@]}")"

run_step() {
  local name="$1"
  shift
  local log="$OUT_ROOT/logs/${name}_$(date +%Y%m%d_%H%M%S).log"
  echo "[$(date)] starting $name"
  echo "logging to $log"
  "$@" 2>&1 | tee "$log"
  echo "[$(date)] finished $name"
}

if [[ "$RUN_CONFIGURE" == "1" ]]; then
  run_step configure \
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DART_BUILD_UPSTREAM_TESTS="$BUILD_UPSTREAM_TESTS"
fi

if [[ "$RUN_BUILD" == "1" ]]; then
  run_step build \
    cmake --build "$BUILD_DIR" -j "$(nproc)"
fi

for bench in "${BENCH_PATHS[@]}" "${SCAN_PATHS[@]}"; do
  if [[ ! -x "$bench" ]]; then
    echo "Missing benchmark executable: $bench" >&2
    echo "Build first, or set BUILD_DIR to the correct build directory." >&2
    exit 1
  fi
done

if [[ "$RUN_TESTS" == "1" ]]; then
  run_step ctest \
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

run_step realistic_all_menus \
  "$PYTHON" scripts/run_benchmarks.py \
    --benches "$BENCHES" \
    --workloads dense_uint64,sparse_uint64,words_fixture,uuid_fixture \
    --sizes "$REALISTIC_SIZES" \
    --runs "$RUNS" \
    --seed "$SEED" \
    --out "$OUT_ROOT/raw/realistic_all_menus.csv"

run_step controlled_fanout_all_menus \
  "$PYTHON" scripts/run_benchmarks.py \
    --benches "$BENCHES" \
    --workloads controlled_fanout \
    --fanouts "$CONTROLLED_FANOUTS" \
    --groups "$CONTROLLED_GROUPS" \
    --runs "$RUNS" \
    --seed "$SEED" \
    --out "$OUT_ROOT/raw/controlled_fanout_all_menus.csv"

run_step zipf_all_menus \
  "$PYTHON" scripts/run_benchmarks.py \
    --benches "$BENCHES" \
    --workloads dense_uint64,sparse_uint64 \
    --sizes "$ZIPF_SIZES" \
    --zipfs "$ZIPFS" \
    --runs "$ZIPF_RUNS" \
    --seed "$SEED" \
    --out "$OUT_ROOT/raw/zipf_all_menus.csv"

run_step scan_realistic_all_menus \
  "$PYTHON" scripts/run_scan_benchmarks.py \
    --benches "$SCAN_BENCHES" \
    --workloads dense_uint64,sparse_uint64,uuid_fixture,words_fixture \
    --sizes "$SCAN_SIZES" \
    --runs "$SCAN_RUNS" \
    --scan-passes "$SCAN_PASSES" \
    --prefix-samples "$PREFIX_SAMPLES" \
    --seed "$SEED" \
    --out "$OUT_ROOT/scan/raw/scan_realistic_all_menus.csv" \
    --overwrite

run_step scan_controlled_fanout_all_menus \
  "$PYTHON" scripts/run_scan_benchmarks.py \
    --benches "$SCAN_BENCHES" \
    --workloads controlled_fanout \
    --fanouts "$CONTROLLED_FANOUTS" \
    --groups "$SCAN_CONTROLLED_GROUPS" \
    --runs "$SCAN_RUNS" \
    --scan-passes "$SCAN_PASSES" \
    --prefix-samples "$PREFIX_SAMPLES" \
    --seed "$SEED" \
    --out "$OUT_ROOT/scan/raw/scan_controlled_fanout_all_menus.csv" \
    --overwrite

tar -czf "${OUT_ROOT}.tgz" "$OUT_ROOT"

echo
echo "[$(date)] all server sweeps complete"
echo "Raw outputs: $OUT_ROOT"
echo "Archive: ${OUT_ROOT}.tgz"
