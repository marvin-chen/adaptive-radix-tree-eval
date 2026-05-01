#!/usr/bin/env python3
"""Run ART benchmark executables and collect raw CSV rows."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def default_bench_path(build_dir: Path) -> Path:
    exe = "bench_original.exe" if os.name == "nt" else "bench_original"
    return build_dir / exe


def parse_csv_list(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def parse_benches(value: str) -> list[Path]:
    benches = []
    for item in parse_csv_list(value):
        if "=" in item:
            _, path = item.split("=", 1)
        else:
            path = item
        bench = Path(path)
        if not bench.is_absolute():
            bench = ROOT / bench
        benches.append(bench)
    return benches


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bench", type=Path, default=None)
    parser.add_argument(
        "--benches",
        default=None,
        help="Comma-separated benchmark executables, optionally label=path.",
    )
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--out", type=Path, default=ROOT / "results" / "raw" / "original4.csv")
    parser.add_argument("--workloads", default="dense_uint64,sparse_uint64,words_fixture,uuid_fixture")
    parser.add_argument("--sizes", default="10000,100000")
    parser.add_argument("--fanouts", default="8,16,17,24,32,33,37,48,49,64,65")
    parser.add_argument("--groups", default="10000")
    parser.add_argument("--zipfs", default="0")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    if args.benches:
        benches = parse_benches(args.benches)
    else:
        benches = [args.bench or default_bench_path(args.build_dir)]

    for bench in benches:
        if not bench.exists():
            raise SystemExit(f"benchmark executable not found: {bench}")

    workloads = parse_csv_list(args.workloads)
    sizes = [int(item) for item in parse_csv_list(args.sizes)]
    fanouts = [int(item) for item in parse_csv_list(args.fanouts)]
    groups = [int(item) for item in parse_csv_list(args.groups)]
    zipfs = [float(item) for item in parse_csv_list(args.zipfs)]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.out.exists():
        args.out.unlink()

    for workload in workloads:
        if workload == "controlled_fanout":
            for group_count in groups:
                for fanout in fanouts:
                    for zipf in zipfs:
                        for bench in benches:
                            cmd = [
                                str(bench),
                                "--workload",
                                workload,
                                "--groups",
                                str(group_count),
                                "--fanout",
                                str(fanout),
                                "--seed",
                                str(args.seed),
                                "--runs",
                                str(args.runs),
                                "--zipf",
                                str(zipf),
                                "--out",
                                str(args.out),
                            ]
                            print(" ".join(cmd))
                            subprocess.run(cmd, cwd=ROOT, check=True)
        else:
            for n in sizes:
                for zipf in zipfs:
                    for bench in benches:
                        cmd = [
                            str(bench),
                            "--workload",
                            workload,
                            "--n",
                            str(n),
                            "--seed",
                            str(args.seed),
                            "--runs",
                            str(args.runs),
                            "--zipf",
                            str(zipf),
                            "--out",
                            str(args.out),
                        ]
                        print(" ".join(cmd))
                        subprocess.run(cmd, cwd=ROOT, check=True)

    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
