#!/usr/bin/env python3
"""Run ART scan benchmark executables into separate scan CSVs."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def exe_name(stem: str) -> str:
    return f"{stem}.exe" if os.name == "nt" else stem


def default_benches(build_dir: Path) -> list[Path]:
    stems = [
        "bench_scan_original",
        "bench_scan_node64",
        "bench_scan_node32_node64",
        "bench_scan_paper6_indexed",
        "bench_scan_node256_only",
    ]
    return [build_dir / exe_name(stem) for stem in stems]


def parse_csv_list(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def parse_benches(value: str) -> list[Path]:
    benches = []
    for item in parse_csv_list(value):
        path = item.split("=", 1)[1] if "=" in item else item
        bench = Path(path)
        benches.append(bench if bench.is_absolute() else ROOT / bench)
    return benches


def parse_prefix_lens(value: str) -> dict[str, list[int]]:
    result = {
        "dense_uint64": [6, 7],
        "sparse_uint64": [1, 2],
        "uuid_fixture": [2, 4],
        "words_fixture": [1, 2],
        "controlled_fanout": [4],
    }
    if not value:
        return result

    for item in parse_csv_list(value):
        workload, text = item.split("=", 1)
        result[workload.strip()] = [int(part) for part in text.split("|") if part]
    return result


def run(cmd: list[str]) -> None:
    print(" ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--benches", default=None)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument(
        "--out",
        type=Path,
        default=ROOT / "results" / "scan" / "raw" / "scan_realistic_sweep.csv",
    )
    parser.add_argument("--workloads", default="dense_uint64,sparse_uint64,uuid_fixture,words_fixture")
    parser.add_argument("--sizes", default="4096,16384,65536,262144,1048576,4194304")
    parser.add_argument("--fanouts", default="1,2,3,4,5,6,8,16,17,32,33,48,49,64,65,128,255")
    parser.add_argument("--groups", default="1000,10000")
    parser.add_argument(
        "--prefix-lens",
        default="",
        help="Comma-separated workload=len|len overrides, e.g. dense_uint64=6|7,uuid_fixture=2|4.",
    )
    parser.add_argument("--scan-passes", type=int, default=3)
    parser.add_argument("--prefix-samples", type=int, default=512)
    parser.add_argument("--runs", type=int, default=15)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    benches = parse_benches(args.benches) if args.benches else default_benches(args.build_dir)
    for bench in benches:
        if not bench.exists():
            raise SystemExit(f"benchmark executable not found: {bench}")

    workloads = parse_csv_list(args.workloads)
    sizes = [int(item) for item in parse_csv_list(args.sizes)]
    fanouts = [int(item) for item in parse_csv_list(args.fanouts)]
    groups = [int(item) for item in parse_csv_list(args.groups)]
    prefix_lens = parse_prefix_lens(args.prefix_lens)

    out = args.out if args.out.is_absolute() else ROOT / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        if not args.overwrite:
            raise SystemExit(f"output already exists, refusing to overwrite: {out}")
        out.unlink()

    for workload in workloads:
        lens = prefix_lens.get(workload, [1])
        if workload == "controlled_fanout":
            for group_count in groups:
                for fanout in fanouts:
                    for bench in benches:
                        base = [
                            str(bench),
                            "--workload", workload,
                            "--groups", str(group_count),
                            "--fanout", str(fanout),
                            "--seed", str(args.seed),
                            "--runs", str(args.runs),
                            "--scan-passes", str(args.scan_passes),
                            "--prefix-samples", str(args.prefix_samples),
                            "--out", str(out),
                        ]
                        run([*base, "--mode", "full", "--prefix-len", str(lens[0])])
                        for prefix_len in lens:
                            run([*base, "--mode", "prefix", "--prefix-len", str(prefix_len)])
        else:
            for n in sizes:
                for bench in benches:
                    base = [
                        str(bench),
                        "--workload", workload,
                        "--n", str(n),
                        "--seed", str(args.seed),
                        "--runs", str(args.runs),
                        "--scan-passes", str(args.scan_passes),
                        "--prefix-samples", str(args.prefix_samples),
                        "--out", str(out),
                    ]
                    run([*base, "--mode", "full", "--prefix-len", str(lens[0])])
                    for prefix_len in lens:
                        run([*base, "--mode", "prefix", "--prefix-len", str(prefix_len)])

    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
