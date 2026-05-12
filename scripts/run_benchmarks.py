#!/usr/bin/env python3
"""Run FlexRML benchmark cases and write a CSV summary."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run FlexRML benchmark cases.")
    parser.add_argument("--benchmark-dir", default="benchmark", help="Directory containing benchmark case folders.")
    parser.add_argument("--binary", default="./flexrml", help="FlexRML executable to run.")
    parser.add_argument("--output-dir", default="benchmark/results", help="Directory for CSV summary and optional outputs.")
    parser.add_argument("--repeats", type=int, default=3, help="Number of runs per case.")
    parser.add_argument("--warmups", type=int, default=0, help="Number of unmeasured warmup runs per case.")
    parser.add_argument("--case", action="append", default=[], help="Run only cases whose directory name contains this text. Can be repeated.")
    parser.add_argument("--build", action="store_true", help="Build the release/default preset before running.")
    parser.add_argument("--no-threading", action="store_true", help="Pass --no-threading to flexrml.")
    parser.add_argument("--keep-outputs", action="store_true", help="Keep generated .nt files under the output directory.")
    parser.add_argument("--csv", default="", help="Explicit CSV result path. Defaults to output-dir/benchmark_<timestamp>.csv.")
    return parser.parse_args()


def discover_cases(benchmark_dir: Path, filters: list[str]) -> list[Path]:
    cases = [
        path.parent
        for path in benchmark_dir.rglob("mapping.rml.ttl")
        if path.is_file()
    ]
    cases.sort(key=lambda path: path.relative_to(benchmark_dir).as_posix())
    if filters:
        cases = [
            path
            for path in cases
            if any(
                filter_text in path.name
                or filter_text in path.relative_to(benchmark_dir).as_posix()
                for filter_text in filters
            )
        ]
    return cases


def category_for_case(benchmark_dir: Path, case_dir: Path) -> str:
    relative = case_dir.relative_to(benchmark_dir)
    if len(relative.parts) > 1:
        return relative.parts[0]
    return "default"


def safe_output_stem(benchmark_dir: Path, case_dir: Path) -> str:
    return case_dir.relative_to(benchmark_dir).as_posix().replace("/", "__")


def parse_time_verbose(stderr: str) -> dict[str, str]:
    fields = {
        "user_seconds": "",
        "system_seconds": "",
        "cpu_percent": "",
        "max_rss_kb": "",
        "exit_status": "",
    }
    patterns = {
        "user_seconds": r"User time \(seconds\):\s*(.+)",
        "system_seconds": r"System time \(seconds\):\s*(.+)",
        "cpu_percent": r"Percent of CPU this job got:\s*(.+)",
        "max_rss_kb": r"Maximum resident set size \(kbytes\):\s*(.+)",
        "exit_status": r"Exit status:\s*(.+)",
    }
    for key, pattern in patterns.items():
        match = re.search(pattern, stderr)
        if match:
            fields[key] = match.group(1).strip()
    return fields


def build_default(root: Path) -> None:
    subprocess.run(["cmake", "--build", "--preset", "default"], cwd=root, check=True)


def run_case(root: Path, binary: Path, case_dir: Path, output_path: Path, no_threading: bool) -> dict[str, str]:
    command = [str(binary), "-m", str(case_dir / "mapping.rml.ttl"), "-o", str(output_path)]
    if no_threading:
        command.append("--no-threading")

    timed_command = command
    if shutil.which("/usr/bin/time"):
        timed_command = ["/usr/bin/time", "-v", *command]

    started = time.perf_counter()
    completed = subprocess.run(timed_command, cwd=root, text=True, capture_output=True)
    wall_seconds = time.perf_counter() - started
    time_fields = parse_time_verbose(completed.stderr)

    return {
        "return_code": str(completed.returncode),
        "wall_seconds": f"{wall_seconds:.6f}",
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
        **time_fields,
    }


def count_lines(path: Path) -> int:
    if not path.exists():
        return 0
    count = 0
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            count += chunk.count(b"\n")
    return count


def parse_float(value: str) -> float | None:
    try:
        return float(value)
    except ValueError:
        return None


def print_category_summary(rows: list[dict[str, str]]) -> None:
    categories: dict[str, dict[str, float]] = {}
    for row in rows:
        if row["return_code"] != "0":
            continue
        wall_seconds = parse_float(row["wall_seconds"])
        max_rss_kb = parse_float(row["max_rss_kb"])
        if wall_seconds is None or max_rss_kb is None:
            continue
        category = row["category"]
        bucket = categories.setdefault(category, {"runs": 0.0, "wall_seconds": 0.0, "max_rss_kb": 0.0})
        bucket["runs"] += 1.0
        bucket["wall_seconds"] += wall_seconds
        bucket["max_rss_kb"] += max_rss_kb

    if not categories:
        return

    print("Category averages:")
    for category in sorted(categories):
        bucket = categories[category]
        runs = int(bucket["runs"])
        print(
            f"  {category}: runs={runs}, "
            f"avg_wall={bucket['wall_seconds'] / runs:.6f}s, "
            f"avg_max_rss={bucket['max_rss_kb'] / runs:.0f} KB"
        )


def main() -> int:
    args = parse_args()
    root = repo_root()
    benchmark_dir = (root / args.benchmark_dir).resolve()
    binary = (root / args.binary).resolve()
    output_dir = (root / args.output_dir).resolve()

    if args.repeats < 1:
        raise SystemExit("--repeats must be at least 1")
    if args.warmups < 0:
        raise SystemExit("--warmups must be at least 0")
    if not benchmark_dir.is_dir():
        raise SystemExit(f"Benchmark directory not found: {benchmark_dir}")
    if args.build:
        build_default(root)
    if not binary.is_file():
        raise SystemExit(f"FlexRML binary not found: {binary}. Run with --build or build first.")

    cases = discover_cases(benchmark_dir, args.case)
    if not cases:
        raise SystemExit("No benchmark cases found.")

    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = Path(args.csv).resolve() if args.csv else output_dir / f"benchmark_{timestamp}.csv"

    rows: list[dict[str, str]] = []
    temp_context = tempfile.TemporaryDirectory(prefix="flexrml_benchmark_") if not args.keep_outputs else None
    generated_dir = output_dir / "outputs" if args.keep_outputs else Path(temp_context.name)
    generated_dir.mkdir(parents=True, exist_ok=True)

    try:
        for case_dir in cases:
            category = category_for_case(benchmark_dir, case_dir)
            output_stem = safe_output_stem(benchmark_dir, case_dir)
            for warmup_index in range(1, args.warmups + 1):
                output_path = generated_dir / f"{output_stem}_warmup{warmup_index}.nt"
                if output_path.exists():
                    output_path.unlink()
                result = run_case(root, binary, case_dir, output_path, args.no_threading)
                status = "ok" if result["return_code"] == "0" else f"failed:{result['return_code']}"
                print(f"{category}/{case_dir.name} warmup {warmup_index}/{args.warmups}: {status}, wall={result['wall_seconds']}s")
                if output_path.exists():
                    output_path.unlink()
                if result["return_code"] != "0":
                    print(result["stderr"], file=sys.stderr)
                    return int(result["return_code"])
            for run_index in range(1, args.repeats + 1):
                output_path = generated_dir / f"{output_stem}_run{run_index}.nt"
                if output_path.exists():
                    output_path.unlink()
                result = run_case(root, binary, case_dir, output_path, args.no_threading)
                row = {
                    "category": category,
                    "case": case_dir.name,
                    "run": str(run_index),
                    "generated_triples": str(count_lines(output_path)),
                    "output_bytes": str(output_path.stat().st_size if output_path.exists() else 0),
                    **{key: value for key, value in result.items() if key not in {"stdout", "stderr"}},
                }
                rows.append(row)
                status = "ok" if result["return_code"] == "0" else f"failed:{result['return_code']}"
                print(f"{category}/{case_dir.name} run {run_index}/{args.repeats}: {status}, wall={result['wall_seconds']}s")
                if result["return_code"] != "0":
                    print(result["stderr"], file=sys.stderr)
                    return int(result["return_code"])
    finally:
        if temp_context is not None:
            temp_context.cleanup()

    fieldnames = [
        "category",
        "case",
        "run",
        "return_code",
        "wall_seconds",
        "user_seconds",
        "system_seconds",
        "cpu_percent",
        "max_rss_kb",
        "generated_triples",
        "output_bytes",
        "exit_status",
    ]
    with csv_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print_category_summary(rows)
    print(f"Wrote benchmark summary: {csv_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
