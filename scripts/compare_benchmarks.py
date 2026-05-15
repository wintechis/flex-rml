#!/usr/bin/env python3
"""Compare two FlexRML benchmark CSV files."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class CaseStats:
    category: str
    case: str
    runs: int
    wall_median: float
    wall_mean: float
    wall_best: float
    rss_median_kb: float
    triples: int
    bytes_out: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare FlexRML benchmark result CSV files.")
    parser.add_argument("baseline", help="Baseline benchmark CSV.")
    parser.add_argument("candidate", help="Candidate benchmark CSV.")
    parser.add_argument("--fail-wall-regression", type=float, default=0.0,
                        help="Exit with status 1 if any common case regresses by this percent or more.")
    parser.add_argument("--min-wall-seconds", type=float, default=0.05,
                        help="Ignore wall-time threshold failures below this baseline median.")
    parser.add_argument("--top", type=int, default=12, help="Number of largest changes to show.")
    parser.add_argument("--category", action="append", default=[], help="Only compare this category. Can be repeated.")
    parser.add_argument("--case", action="append", default=[], help="Only compare cases containing this text. Can be repeated.")
    return parser.parse_args()


def parse_float(value: str) -> float | None:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def parse_int(value: str) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def load_stats(path: Path, categories: set[str], case_filters: list[str]) -> dict[tuple[str, str], CaseStats]:
    samples: dict[tuple[str, str], list[dict[str, str]]] = {}
    with path.open(newline="") as handle:
        for row in csv.DictReader(handle):
            if row.get("return_code") != "0":
                continue
            category = row.get("category", "")
            case = row.get("case", "")
            if categories and category not in categories:
                continue
            if case_filters and not any(text in case or text in f"{category}/{case}" for text in case_filters):
                continue
            wall = parse_float(row.get("wall_seconds", ""))
            rss = parse_float(row.get("max_rss_kb", ""))
            if wall is None or rss is None:
                continue
            samples.setdefault((category, case), []).append(row)

    stats: dict[tuple[str, str], CaseStats] = {}
    for (category, case), rows in samples.items():
        walls = [parse_float(row["wall_seconds"]) for row in rows]
        rss_values = [parse_float(row["max_rss_kb"]) for row in rows]
        valid_walls = [value for value in walls if value is not None]
        valid_rss = [value for value in rss_values if value is not None]
        if not valid_walls or not valid_rss:
            continue
        triples = max(parse_int(row.get("generated_triples", "0")) for row in rows)
        bytes_out = max(parse_int(row.get("output_bytes", "0")) for row in rows)
        stats[(category, case)] = CaseStats(
            category=category,
            case=case,
            runs=len(valid_walls),
            wall_median=statistics.median(valid_walls),
            wall_mean=statistics.fmean(valid_walls),
            wall_best=min(valid_walls),
            rss_median_kb=statistics.median(valid_rss),
            triples=triples,
            bytes_out=bytes_out,
        )
    return stats


def pct_delta(candidate: float, baseline: float) -> float:
    if baseline == 0:
      return math.inf if candidate > 0 else 0.0
    return (candidate - baseline) / baseline * 100.0


def fmt_seconds(value: float) -> str:
    return f"{value:.6f}s"


def fmt_pct(value: float) -> str:
    if math.isinf(value):
        return "inf%"
    return f"{value:+.1f}%"


def fmt_mb(kb: float) -> str:
    return f"{kb / 1024:.1f}MB"


def print_case_table(title: str, rows: list[tuple[float, tuple[str, str], CaseStats, CaseStats]], top: int) -> None:
    if not rows:
        return
    print(title)
    print("  case, baseline, candidate, delta, rss_delta")
    for _, key, base, cand in rows[:top]:
        wall_delta = pct_delta(cand.wall_median, base.wall_median)
        rss_delta = pct_delta(cand.rss_median_kb, base.rss_median_kb)
        print(
            f"  {key[0]}/{key[1]}, "
            f"{fmt_seconds(base.wall_median)}, {fmt_seconds(cand.wall_median)}, "
            f"{fmt_pct(wall_delta)}, {fmt_pct(rss_delta)}"
        )


def print_category_summary(common: set[tuple[str, str]],
                           baseline: dict[tuple[str, str], CaseStats],
                           candidate: dict[tuple[str, str], CaseStats]) -> None:
    categories = sorted({category for category, _ in common})
    if not categories:
        return
    print("Category summary")
    print("  category, cases, baseline_avg_median, candidate_avg_median, wall_delta, rss_delta")
    for category in categories:
        keys = [key for key in common if key[0] == category]
        base_wall = statistics.fmean(baseline[key].wall_median for key in keys)
        cand_wall = statistics.fmean(candidate[key].wall_median for key in keys)
        base_rss = statistics.fmean(baseline[key].rss_median_kb for key in keys)
        cand_rss = statistics.fmean(candidate[key].rss_median_kb for key in keys)
        print(
            f"  {category}, {len(keys)}, "
            f"{fmt_seconds(base_wall)}, {fmt_seconds(cand_wall)}, "
            f"{fmt_pct(pct_delta(cand_wall, base_wall))}, "
            f"{fmt_pct(pct_delta(cand_rss, base_rss))}"
        )


def main() -> int:
    args = parse_args()
    categories = set(args.category)
    baseline_path = Path(args.baseline)
    candidate_path = Path(args.candidate)
    baseline = load_stats(baseline_path, categories, args.case)
    candidate = load_stats(candidate_path, categories, args.case)
    common = set(baseline) & set(candidate)
    missing = set(baseline) - set(candidate)
    added = set(candidate) - set(baseline)

    if not common:
        raise SystemExit("No common successful benchmark cases to compare.")

    print(f"Baseline:  {baseline_path}")
    print(f"Candidate: {candidate_path}")
    print(f"Common successful cases: {len(common)}")
    if missing:
        print(f"Missing in candidate: {len(missing)}")
    if added:
        print(f"Only in candidate: {len(added)}")
    print()

    print_category_summary(common, baseline, candidate)
    print()

    changes: list[tuple[float, tuple[str, str], CaseStats, CaseStats]] = []
    regressions_over_threshold: list[tuple[str, str, float]] = []
    for key in sorted(common):
        base = baseline[key]
        cand = candidate[key]
        delta = pct_delta(cand.wall_median, base.wall_median)
        changes.append((delta, key, base, cand))
        if (args.fail_wall_regression > 0 and
                base.wall_median >= args.min_wall_seconds and
                delta >= args.fail_wall_regression):
            regressions_over_threshold.append((key[0], key[1], delta))

    print_case_table("Largest speedups", sorted(changes, key=lambda item: item[0]), args.top)
    print()
    print_case_table("Largest regressions", sorted(changes, key=lambda item: item[0], reverse=True), args.top)

    if regressions_over_threshold:
        print()
        print(f"Wall-time regressions >= {args.fail_wall_regression:.1f}%:")
        for category, case, delta in regressions_over_threshold:
            print(f"  {category}/{case}: {fmt_pct(delta)}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
