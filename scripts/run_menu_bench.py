#!/usr/bin/env python3
"""
Run ART menu variants and collect comparable node-type overhead metrics to CSV.

Executes all menu-count benchmark variants across workloads and sizes,
parsing per-run statistics and writing to a comprehensive CSV for analysis
of how node-type count affects performance and structure.
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import subprocess
from pathlib import Path
from typing import Optional


ROOT = Path(__file__).resolve().parents[1]

# Menu variants to test: (bench_target_name, display_name, node_count, node_types_str)
MENU_VARIANTS = [
    ("bench_count1_node256", "count1_node256", 1, "NODE256"),
    ("bench_count2_16_256", "count2_16_256", 2, "NODE16,NODE256"),
    ("bench_count3_4_16_256", "count3_4_16_256", 3, "NODE4,NODE16,NODE256"),
    ("bench_count4_original4", "count4_original4", 4, "NODE4,NODE16,NODE48,NODE256"),
    ("bench_count5_4_16_32_64_256", "count5_4_16_32_64_256", 5, "NODE4,NODE16,NODE32,NODE64,NODE256"),
    ("bench_count6_4_16_32_48_64_256", "count6_4_16_32_48_64_256", 6, "NODE4,NODE16,NODE32,NODE48,NODE64,NODE256"),
    ("bench_count6_paper6", "count6_paper6", 6, "NODE2,NODE5,NODE16,NODE32,NODE48,NODE64,NODE256"),
    ("bench_count7_2_5_16_32_48_64_256", "count7_2_5_16_32_48_64_256", 7, "NODE2,NODE5,NODE16,NODE32,NODE48,NODE64,NODE256"),
]

# Workload presets
WORKLOAD_SETS = {
    "quick": {
        "dense_uint64": [10000],
        "sparse_uint64": [10000],
        "controlled_fanout": [(1000, 10, 0), (1000, 32, 0)],
    },
    "standard": {
        "dense_uint64": [100000, 1000000],
        "sparse_uint64": [100000, 1000000],
        "controlled_fanout": [(10000, 10, 0), (10000, 16, 0), (10000, 32, 0)],
    },
    "full": {
        "dense_uint64": [10000, 100000, 1000000],
        "sparse_uint64": [10000, 100000, 1000000],
        "words_fixture": [None],
        "uuid_fixture": [None],
        "controlled_fanout": [(1000, 10, 0), (1000, 16, 0), (1000, 32, 0), 
                               (10000, 10, 0), (10000, 16, 0), (10000, 32, 0)],
    },
}


def find_bench_executable(build_dir: Path, target_name: str) -> Optional[Path]:
    """Locate a benchmark executable in the build directory."""
    exe_name = target_name + (".exe" if os.name == "nt" else "")
    exe_path = build_dir / exe_name
    if exe_path.exists():
        return exe_path
    return None


def build_menu_benchmarks(build_dir: Path) -> int:
    """Build all menu benchmark targets."""
    print("Building menu benchmark targets...")
    targets = [name for name, _, _, _ in MENU_VARIANTS]
    for target in targets:
        cmd = ["cmake", "--build", str(build_dir), "--target", target, "-j"]
        print(f"  Building {target}...")
        result = subprocess.run(cmd, cwd=ROOT)
        if result.returncode != 0:
            print(f"ERROR: Failed to build {target}")
            return 1
    return 0


def run_benchmark(
    exe_path: Path,
    workload: str,
    output_csv: Path,
    n: Optional[int] = None,
    groups: Optional[int] = None,
    fanout: Optional[int] = None,
    seed: int = 1,
    runs: int = 3,
) -> int:
    """Run a single benchmark with specified workload, writing CSV output."""
    cmd = [str(exe_path), "--workload", workload, "--seed", str(seed), "--runs", str(runs),
           "--out", str(output_csv)]
    
    if workload == "controlled_fanout":
        if groups is not None:
            cmd.extend(["--groups", str(groups)])
        if fanout is not None:
            cmd.extend(["--fanout", str(fanout)])
    else:
        if n is not None:
            cmd.extend(["--n", str(n)])

    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ERROR running {exe_path.name}: {result.stderr}")
        return 1
    return 0


def run_menu_experiments(
    build_dir: Path,
    workload_preset: str,
    output_csv: Path,
    runs: int = 3,
) -> int:
    """Run all menu variants across workloads and collect results."""
    output_csv.parent.mkdir(parents=True, exist_ok=True)
    
    workloads = WORKLOAD_SETS.get(workload_preset, WORKLOAD_SETS["standard"])
    
    # Prepare master CSV writer with bench output columns plus menu metadata
    bench_cols = [
        "variant", "workload", "n", "seed", "fanout_target", "groups", "zipf_s", "run", "operation",
        "seconds", "ops", "mops_per_sec", "keys", "node4", "node16", "node32", "node48", "node256",
        "internal_nodes", "leaves", "internal_node_bytes", "leaf_bytes", "total_bytes", "bytes_per_key",
        "avg_leaf_depth", "max_leaf_depth", "fanout_0_4", "fanout_5_16", "fanout_17_32", "fanout_33_48",
        "fanout_49_64", "fanout_65_256", "node64", "node4_bytes", "node16_bytes", "node32_bytes",
        "node48_bytes", "node64_bytes", "node256_bytes", "node2", "node5", "node2_bytes", "node5_bytes",
        "fanout_1_2", "fanout_3_5", "fanout_6_16", "unique_keys", "duplicate_keys",
    ]
    output_cols = ["menu", "node_count", "node_types"] + bench_cols
    
    csv_file = open(output_csv, "w", newline="")
    writer = csv.DictWriter(csv_file, fieldnames=output_cols)
    writer.writeheader()
    
    total_runs = sum(
        len(sizes) if sizes else 1
        for sizes in workloads.values()
    ) * len(MENU_VARIANTS) * runs
    run_count = 0
    
    for menu_target, menu_name, node_count, node_types in MENU_VARIANTS:
        exe_path = find_bench_executable(build_dir, menu_target)
        if not exe_path:
            print(f"WARNING: Benchmark executable {menu_target} not found, skipping")
            continue
        
        print(f"\nRunning {menu_name} ({node_count} node types: {node_types})")
        
        for workload, configs in workloads.items():
            if not configs:
                configs = [None]
            
            for config in configs:
                # Create a temp CSV file for this benchmark run
                temp_csv = output_csv.parent / f"temp_{menu_target}_{workload}_{config}.csv"
                
                for trial in range(runs):
                    run_count += 1
                    pct = 100 * run_count // total_runs if total_runs > 0 else 0
                    print(f"  [{pct:3d}%] {workload} config={config} trial={trial+1}/{runs}", end="\r")
                    
                    if workload == "controlled_fanout":
                        groups, fanout = config[:2]
                        n = None
                    else:
                        n = config
                        groups, fanout = None, None
                    
                    if run_benchmark(exe_path, workload, temp_csv, n=n, groups=groups, fanout=fanout, runs=1) != 0:
                        continue
                    
                    # Read temp CSV and add menu metadata
                    try:
                        with open(temp_csv, "r") as f:
                            reader = csv.DictReader(f)
                            for row in reader:
                                row["menu"] = menu_name
                                row["node_count"] = node_count
                                row["node_types"] = node_types
                                writer.writerow(row)
                    except Exception as e:
                        print(f"ERROR reading {temp_csv}: {e}")
                    
                    # Clean up temp file
                    if temp_csv.exists():
                        temp_csv.unlink()
    
    csv_file.close()
    print(f"\n✓ Wrote {output_csv}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run ART menu variants and collect node-type overhead metrics."
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build",
        help="CMake build directory",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=ROOT / "output" / "menu_count_bench.csv",
        help="Output CSV path",
    )
    parser.add_argument(
        "--workloads",
        choices=list(WORKLOAD_SETS.keys()),
        default="standard",
        help="Workload preset",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=3,
        help="Trials per configuration",
    )
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Skip building; assume binaries exist",
    )
    args = parser.parse_args()

    if not args.no_build:
        if build_menu_benchmarks(args.build_dir) != 0:
            return 1

    return run_menu_experiments(args.build_dir, args.workloads, args.out, runs=args.runs)


if __name__ == "__main__":
    raise SystemExit(main())
