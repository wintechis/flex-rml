#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEST_CASES_DIR = ROOT / "test_cases"


@dataclass
class CaseResult:
    name: str
    passed: bool
    expected_error: bool
    return_code: int
    reason: str
    details: str = ""
    stderr: str = ""


def parse_error_expected(readme_path: Path) -> bool:
    content = readme_path.read_text(encoding="utf-8")
    match = re.search(r"\*\*Error expected\?\*\*\s*(Yes|No)", content, re.IGNORECASE)
    if not match:
        raise ValueError(f"Could not find 'Error expected?' in {readme_path}")
    return match.group(1).strip().lower() == "yes"


def normalize_output(output: str) -> list[str]:
    lines = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        lines.append(line)
    return sorted(lines)


def run_case(case_dir: Path) -> CaseResult:
    expected_error = parse_error_expected(case_dir / "README.md")
    with tempfile.NamedTemporaryFile(
        suffix=f"_{case_dir.name}.nq",
        prefix="flexrml_validation_",
        delete=False,
    ) as handle:
        output_path = Path(handle.name)

    if output_path.exists():
        output_path.unlink()

    env = os.environ.copy()
    env["PYTHONPATH"] = str(ROOT / "src")

    proc = subprocess.run(
        [sys.executable, "-m", "flexrml.flexcore", "-m", "mapping.ttl", "-o", str(output_path)],
        cwd=case_dir,
        env=env,
        capture_output=True,
        text=True,
    )

    stdout = proc.stdout.strip()
    stderr = proc.stderr.strip()

    try:
        if expected_error:
            passed = proc.returncode != 0
            reason = "error observed as expected" if passed else "expected an error, but execution succeeded"
            return CaseResult(case_dir.name, passed, expected_error, proc.returncode, reason, stdout, stderr)

        if proc.returncode != 0:
            return CaseResult(
                case_dir.name,
                False,
                expected_error,
                proc.returncode,
                "unexpected execution error",
                stdout,
                stderr,
            )

        actual_output = output_path.read_text(encoding="utf-8") if output_path.exists() else ""
        expected_output = (case_dir / "output.nq").read_text(encoding="utf-8")

        actual_lines = normalize_output(actual_output)
        expected_lines = normalize_output(expected_output)

        if actual_lines != expected_lines:
            return CaseResult(
                case_dir.name,
                False,
                expected_error,
                proc.returncode,
                "output mismatch",
                f"expected:\n{expected_output.strip()}\nactual:\n{actual_output.strip()}",
                stderr,
            )

        return CaseResult(case_dir.name, True, expected_error, proc.returncode, "output matches", stdout, stderr)
    finally:
        output_path.unlink(missing_ok=True)


def iter_case_dirs(selected: list[str] | None) -> list[Path]:
    case_dirs = sorted(path for path in TEST_CASES_DIR.iterdir() if path.is_dir())
    if not selected:
        return case_dirs
    selected_set = set(selected)
    return [case_dir for case_dir in case_dirs if case_dir.name in selected_set]


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate FlexRML against local RML test cases.")
    parser.add_argument("cases", nargs="*", help="Optional list of case directory names to run.")
    args = parser.parse_args()

    case_dirs = iter_case_dirs(args.cases)
    if not case_dirs:
        print("No matching test cases found.", file=sys.stderr)
        return 2

    results = [run_case(case_dir) for case_dir in case_dirs]

    for result in results:
        status = "PASS" if result.passed else "FAIL"
        print(f"{status} {result.name}: {result.reason}")
        if not result.passed:
            print(f"  return code: {result.return_code}")
            if result.details:
                print("  details:")
                for line in result.details.splitlines():
                    print(f"    {line}")
            if result.stderr:
                print("  stderr:")
                for line in result.stderr.splitlines():
                    print(f"    {line}")

    passed_count = sum(result.passed for result in results)
    print(f"\nSummary: {passed_count}/{len(results)} cases passed.")
    return 0 if passed_count == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
