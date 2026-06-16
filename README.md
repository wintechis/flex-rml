# FlexRML - experimental. really fast. stability not guaranteed.

FlexRML is an experimental native C++ RML processor. The goal is to be fast and memory efficient.

## Description

RML (RDF Mapping Language) is central to knowledge acquisition. FlexRML is a flexible RML processor able to run on a wide range of devices:

- Cloud Environments
- Consumer Hardware
- Single Board Computers
- Microcontrollers (Separate Repository)

Currently, FlexRML supports CSV, JSON, and XML logical sources. CSV is read as
rows, JSON supports JSONPath-style iterators for object arrays, and XML supports
XPath iterators through the shared source reader abstraction.

## Performance

The benchmark numbers below are from a local run on a Ryzen 5 7500F with 6 cores using the
default CMake build and the built-in benchmark runner:

```bash
python3 scripts/run_benchmarks.py --build --output-dir bench_res --repeats 3 --warmups 1
```

The run completed all benchmark cases with no failures. Times are average wall
clock time per measured run. Memory is average peak RSS per run. Results are
hardware and dataset dependent.

| Category | Cases | Avg wall time | Avg CPU | Avg peak RSS | Avg generated triples |
| --- | ---: | ---: | ---: | ---: | ---: |
| GTFS | 4 | 2.124 s | 417% | 1.62 GiB | 12,868,472 |
| duplicates | 5 | 0.083 s | 828% | 53.1 MiB | 1,000,016 |
| empty | 5 | 0.069 s | 794% | 54.8 MiB | 1,000,000 |
| join | 25 | 0.096 s | 101% | 32.3 MiB | 117,060 |
| mappings | 4 | 0.375 s | 595% | 206.7 MiB | 7,625,000 |
| namedgraph | 15 | 1.083 s | 712% | 529.2 MiB | 16,200,000 |
| raw | 9 | 2.358 s | 813% | 659.1 MiB | 39,144,444 |

GTFS benchmark details:

| Case | Avg wall time | Avg CPU | Avg peak RSS | Generated triples | Output size |
| --- | ---: | ---: | ---: | ---: | ---: |
| `100_csv` | 5.709 s | 343% | 875.1 MiB | 39,595,300 | 7.00 GiB |
| `10_csv` | 0.529 s | 346% | 216.5 MiB | 3,959,530 | 0.70 GiB |
| `10_json` | 0.886 s | 504% | 1.23 GiB | 3,959,530 | 0.70 GiB |
| `10_xml` | 1.373 s | 477% | 4.17 GiB | 3,959,530 | 0.70 GiB |

## Installation

### Using Prebuilt Binaries
Prebuilt binaries for Debian based systems are available in the [releases](https://github.com/wintechis/flex-rml/releases) section. 

### Compiling from Source

**Prerequisites**
We test on Ubuntu 24.04 LTS with GCC 13.3.

Install a C++ toolchain, CMake, and pkg-config:
```bash
sudo apt install build-essential cmake pkg-config
```

Native dependencies are managed with vcpkg manifest mode:

- `jsoncons`
- `pugixml`
- `serd`
- `unordered-dense`
- `xxhash`

Install vcpkg, make sure `vcpkg` is on your `PATH`, then install dependencies from the project root:

```bash
vcpkg install
```

The CMake build detects dependencies in `vcpkg_installed/<triplet>` when you use vcpkg manifest mode. If you use a classic vcpkg checkout instead, configure CMake with the vcpkg toolchain file:

```bash
cmake --preset default -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

**Compilation Process:**

1. Clone or download the repository from GitHub and navigate to the project directory.
```bash
git clone git@github.com:wintechis/flex-rml.git
cd flex-rml
```

2. Install C++ dependencies.

```bash
vcpkg install
```

3. Build the native executable with CMake.

```bash
cmake --preset default
cmake --build --preset default
```

The build produces one executable:

```bash
./flexrml
```

Useful build overrides:

```bash
cmake --preset test
cmake --build --preset test
cmake --preset debug
cmake --build --preset debug
cmake --preset default -DVCPKG_TARGET_TRIPLET=x64-linux
```

Use the `test` preset for fast local rebuilds while working on code. It disables optimization and debug-symbol generation.

The build produces one CLI executable. Dependency linkage depends on the vcpkg triplet and system packages, the executable still depends on normal system runtime libraries such as `libstdc++` and `libc`.

### Versioning

The project version is set in `CMakeLists.txt`:

```cmake
project(flexrml VERSION 3.0.0 LANGUAGES CXX)
```

CMake generates the runtime version header from that value. Check the built executable with:

```bash
./flexrml --version
```

## Getting Started

To execute a mapping and print triples to stdout:

```bash
./flexrml -m mapping.rml.ttl
```

To pass a mapping directly as a string:

```bash
./flexrml --mapping-string '@prefix rml: <http://w3id.org/rml/> . ...'
```

To write triples to a file:

```bash
./flexrml -m mapping.rml.ttl -o output.nt
```

Useful CLI options:

```bash
./flexrml -m mapping.rml.ttl -b http://example.com/base/
./flexrml -m mapping.rml.ttl --no-threading
./flexrml -m mapping.rml.ttl -gp
./flexrml --version
./flexrml --help
```

### In-memory sources

Mappings can bind logical sources to runtime strings instead of files. Use an
SD dataset specification with `sd:name`; the name is matched against repeated
`--source` options:

```turtle
@prefix rml: <http://w3id.org/rml/> .
@prefix sd: <https://w3id.org/okn/o/sd#> .

<#LogicalSource>
    a rml:LogicalSource ;
    rml:source <#RuntimeData> ;
    rml:referenceFormulation rml:JSONPath ;
    rml:iterator "$[*]" .

<#RuntimeData>
    a sd:DatasetSpecification ;
    sd:name "data" .
```

Pass the payload as a literal string or read it from a file:

```bash
./flexrml -m mapping.rml.ttl --source 'data=[{"id":1}]'
./flexrml -m mapping.rml.ttl --source data=@payload.json
./flexrml --mapping-string '...' --source data=@payload.json
```

The mapping chooses the parser through `rml:referenceFormulation`. In-memory
CSV uses `rml:CSV`, JSON uses `rml:JSONPath`, and XML uses `rml:XPath`.
File-based sources continue to work when no matching `--source` is provided.

## Architecture

FlexRML is structured as a frontend/backend pipeline. The frontend parses and normalizes mappings into an intermediate representation. The backend plans, optimizes, and executes typed programs against source readers.

The intended layering is:

```text
frontend -> backend/planner -> backend/optimizer -> backend/program -> backend/source -> backend/executor
```

Source handling is implemented in C++ under `src/flexrml/backend/source/`. CSV,
JSON, and XML are exposed to the executors through the same row-oriented interface.

## Conformance

FlexRML passes the configured validation categories for RML-Core JSON cases and RML-FNML cases. The test data itself is not tracked in this repository, copy the suites into `test_cases/` before running validation.

The runtime is C++. Python is only used for validation tooling. To run conformance validation, install the Python test dependency:

```bash
python3 -m venv env
source env/bin/activate
pip install -r requirements.txt
```

Place the official test cases in `test_cases/`. Category subfolders such as
`test_cases/rml-core/` and `test_cases/rml-fnml/` are supported. Build and run validation through CMake with:

```bash
cmake --build --preset test --target validate
```

You can also run the validator directly:

```bash
python scripts/validate_test_cases.py
```

To run the validator against the test binary explicitly:

```bash
FLEXRML_BINARY=./flexrml_test python scripts/validate_test_cases.py
```

You can also run a category or a single case by name:

```bash
python scripts/validate_test_cases.py rml-core
python scripts/validate_test_cases.py rml-core/RMLTC0000-JSON
```

The validator also generates a Markdown report at `validation_report.md`.

## Benchmarking

Benchmark cases live under `benchmark/`. Each case directory must contain a
`mapping.rml.ttl` file. Run the benchmark suite with warmups and repeated
measured runs:

```bash
cmake --build --preset default
python scripts/run_benchmarks.py --repeats 5 --warmups 1
```

There is also a CMake target for the default benchmark run:

```bash
cmake --build --preset default --target benchmark
```

For focused optimization work, run only selected cases:

```bash
python scripts/run_benchmarks.py --case namedgraph --case mappings_10_5 --repeats 5 --warmups 1
```

The script prints wall time and peak RSS for each run, writes CSV files to
`benchmark/results/`, and removes generated `.nt` files by default. Use
`--keep-outputs` when you need to inspect generated triples. Compare a candidate
result against a baseline with:

```bash
python scripts/compare_benchmarks.py benchmark/results/baseline.csv benchmark/results/candidate.csv
```

To fail a check when any common case regresses by at least 10 percent:

```bash
python scripts/compare_benchmarks.py benchmark/results/baseline.csv benchmark/results/candidate.csv --fail-wall-regression 10
```

## Microcontroller Compatible Version

For those working with Microcontrollers like ESP32, we have a dedicated version of this project. It's made specifically for compatibility with the Arduino IDE. You can access it and find detailed instructions for setup and use at the following link:
[FlexRML ESP32 Repository](https://github.com/wintechis/flex-rml-esp32/tree/main)

## Citation

If you use this work in your research, please cite it as:

```bibtex
@article{Freund_FlexRML_A_Flexible_2024,
  author = {Freund, Michael and Schmid, Sebastian and Dorsch, Rene and Harth, Andreas},
  journal = {Extended Semantic Web Conference},
  title = {{FlexRML: A Flexible and Memory Efficient Knowledge Graph Materializer}},
  year = {2024}
}
```

```bibtex
@article{Freund_efficient_construction_2025,
  author = {Freund, Michael and Schmid, Sebastian and Harth, Andreas},
  journal = {Linking Meaning: Semantic Technologies Shaping the Future of AI},
  title = {{Efficient Knowledge Graph Construction Based on Optimized Plans}},
  year = {2025}
}
```

## Licenses

### Project License

This project is licensed under the GNU Affero General Public License version 3 (AGPLv3). The full text of the license can be found in the `LICENSE` file in this repository.

### External C++ Libraries
This project uses external C++ libraries managed through vcpkg:

- [Serd](https://github.com/drobilla/serd) is licensed under the ISC License.
- [jsoncons](https://github.com/danielaparker/jsoncons) is licensed under the Boost Software License 1.0.
- [pugixml](https://pugixml.org/) is licensed under the MIT License.
- [xxHash](https://github.com/Cyan4973/xxHash) is licensed under the BSD 2-Clause License.
- [unordered_dense](https://github.com/martinus/unordered_dense) is licensed under the MIT License.
