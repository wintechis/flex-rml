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
