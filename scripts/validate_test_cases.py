#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

try:
    from rdflib import Dataset
    from rdflib.compare import to_isomorphic
except ImportError:  # pragma: no cover - handled at runtime for missing deps
    Dataset = None
    to_isomorphic = None


ROOT = Path(__file__).resolve().parents[1]
TEST_CASES_DIR = ROOT / "test_cases"
REPORT_PATH = ROOT / "validation_report.md"
PYPROJECT_PATH = ROOT / "pyproject.toml"
STANDALONE_BINARY = ROOT / "flexrml"

# Toggle this to validate test cases with the built standalone binary instead
# of `python -m flexrml.flexcore`.
USE_STANDALONE_BINARY = False


@dataclass
class CaseResult:
    name: str
    passed: bool
    expected_error: bool
    return_code: int
    reason: str
    details: str = ""
    stderr: str = ""


def build_flexrml_command(output_path: Path, default_base_iri: str | None) -> tuple[list[str], dict[str, str]]:
    if USE_STANDALONE_BINARY:
        if not STANDALONE_BINARY.is_file():
            raise FileNotFoundError(
                f"Standalone binary not found at {STANDALONE_BINARY}. Build it first or set "
                "USE_STANDALONE_BINARY = False."
            )

        command = [str(STANDALONE_BINARY), "-m", "mapping.ttl", "-o", str(output_path), "--validate-shacl"]
        env = os.environ.copy()
    else:
        command = [sys.executable, "-m", "flexrml.flexcore", "-m", "mapping.ttl", "-o", str(output_path), "--validate-shacl"]
        env = os.environ.copy()
        env["PYTHONPATH"] = str(ROOT / "src")

    if default_base_iri:
        command.extend(["-b", default_base_iri])

    return command, env


def parse_error_expected(readme_path: Path) -> bool:
    content = readme_path.read_text(encoding="utf-8")
    match = re.search(r"\*\*Error expected\?\*\*\s*(Yes|No)", content, re.IGNORECASE)
    if not match:
        raise ValueError(f"Could not find 'Error expected?' in {readme_path}")
    return match.group(1).strip().lower() == "yes"


def parse_default_base_iri(readme_path: Path) -> str | None:
    content = readme_path.read_text(encoding="utf-8")
    match = re.search(r"\*\*Default Base IRI\*\*\s*:\s*(\S+)", content, re.IGNORECASE)
    if not match:
        return None
    return match.group(1).strip()


def normalize_output(output: str) -> list[str]:
    lines = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        line = " ".join(line.split())
        lines.append(line)
    return sorted(lines)


TERM_RE = r'(?:<[^>]*>|_:[^\s]+|"(?:[^"\\]|\\.)*"(?:@[A-Za-z0-9-]+|\^\^<[^>]*>)?)'
TRIPLE_RE = re.compile(
    rf"^({TERM_RE})\s+(<[^>]*>)\s+({TERM_RE})(?:\s+({TERM_RE}))?\s+\.$"
)


def parse_rdf_lines(output: str) -> list[tuple[str, str, str, str | None]]:
    triples = []
    for line in normalize_output(output):
        match = TRIPLE_RE.match(line)
        if not match:
            raise ValueError(f"Could not parse RDF line: {line}")
        subject, predicate, obj, graph = match.groups()
        triples.append((subject, predicate, obj, graph))
    return triples


def parse_with_rdflib(output: str) -> Dataset:
    if Dataset is None:
        raise RuntimeError(
            "rdflib is required for RDF result comparison. Install dependencies with "
            "`pip install -r requirements.txt`."
        )

    cleaned_output = "\n".join(normalize_output(output))
    last_error: Exception | None = None
    for rdf_format in ("nquads", "nt"):
        dataset = Dataset()
        try:
            dataset.parse(data=cleaned_output, format=rdf_format)
            return dataset
        except Exception as exc:
            last_error = exc

    raise ValueError(f"Could not parse RDF output with rdflib: {last_error}")


def dataset_context_map(dataset: Dataset) -> dict[str, object]:
    contexts = {}
    for graph in dataset.graphs():
        contexts[str(graph.identifier)] = graph
    return contexts


def compare_rdf_outputs(expected_output: str, actual_output: str) -> bool:
    try:
        expected_dataset = parse_with_rdflib(expected_output)
        actual_dataset = parse_with_rdflib(actual_output)
    except ValueError:
        return False

    expected_contexts = dataset_context_map(expected_dataset)
    actual_contexts = dataset_context_map(actual_dataset)

    if set(expected_contexts) != set(actual_contexts):
        return False

    for identifier in expected_contexts:
        if to_isomorphic(expected_contexts[identifier]) != to_isomorphic(actual_contexts[identifier]):
            return False

    return True


def run_case(case_dir: Path) -> CaseResult:
    readme_path = case_dir / "README.md"
    expected_error = parse_error_expected(readme_path)
    default_base_iri = parse_default_base_iri(readme_path)
    with tempfile.NamedTemporaryFile(
        suffix=f"_{case_dir.name}.nq",
        prefix="flexrml_validation_",
        delete=False,
    ) as handle:
        output_path = Path(handle.name)

    if output_path.exists():
        output_path.unlink()

    command, env = build_flexrml_command(output_path, default_base_iri)

    proc = subprocess.run(
        command,
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

        outputs_match = actual_lines == expected_lines
        if not outputs_match:
            outputs_match = compare_rdf_outputs(expected_output, actual_output)

        if not outputs_match:
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


def parse_project_version() -> str:
    if not PYPROJECT_PATH.exists():
        return "unknown"

    content = PYPROJECT_PATH.read_text(encoding="utf-8")
    match = re.search(r'^version\s*=\s*"([^"]+)"', content, re.MULTILINE)
    if not match:
        return "unknown"

    return match.group(1)


def write_markdown_report(results: list[CaseResult], selected: list[str] | None) -> None:
    passed_count = sum(result.passed for result in results)
    failed = [result for result in results if not result.passed]
    scope = ", ".join(selected) if selected else "all test cases"
    generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")
    version = parse_project_version()

    lines = [
        "# Validation Report",
        "",
        f"- FlexRML version: `{version}`",
        f"- Generated at: `{generated_at}`",
        f"- Scope: `{scope}`",
        f"- Execution mode: `{'standalone binary' if USE_STANDALONE_BINARY else 'python module'}`",
        f"- Passed: `{passed_count}/{len(results)}`",
        f"- Failed: `{len(failed)}`",
        "",
        "## Results",
        "",
    ]

    for result in results:
        status = "PASS" if result.passed else "FAIL"
        lines.append(f"- `{status}` `{result.name}`: {result.reason}")

    if failed:
        lines.extend([
            "",
            "## Failures",
            "",
        ])
        for result in failed:
            lines.append(f"### `{result.name}`")
            lines.append("")
            lines.append(f"- Return code: `{result.return_code}`")
            lines.append(f"- Reason: {result.reason}")
            if result.details:
                lines.append("- Details:")
                lines.append("```text")
                lines.append(result.details)
                lines.append("```")
            if result.stderr:
                lines.append("- Stderr:")
                lines.append("```text")
                lines.append(result.stderr)
                lines.append("```")
            lines.append("")

    REPORT_PATH.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate FlexRML against local RML test cases.")
    parser.add_argument("cases", nargs="*", help="Optional list of case directory names to run.")
    args = parser.parse_args()

    case_dirs = iter_case_dirs(args.cases)
    if not case_dirs:
        print("No matching test cases found.", file=sys.stderr)
        return 2

    results = []
    for case_dir in case_dirs:
        result = run_case(case_dir)
        results.append(result)
        status = "PASS" if result.passed else "FAIL"
        print(f"{status} {result.name}: {result.reason}", flush=True)
        if not result.passed:
            print(f"  return code: {result.return_code}", flush=True)
            if result.details:
                print("  details:", flush=True)
                for line in result.details.splitlines():
                    print(f"    {line}", flush=True)
            if result.stderr:
                print("  stderr:", flush=True)
                for line in result.stderr.splitlines():
                    print(f"    {line}", flush=True)

    passed_count = sum(result.passed for result in results)
    write_markdown_report(results, args.cases or None)
    print(f"\nSummary: {passed_count}/{len(results)} cases passed.")
    return 0 if passed_count == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
