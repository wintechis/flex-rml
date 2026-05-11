# FlexRML - experimental. really fast. stability not guaranteed.

FlexRML is an experimental native C++ RML processor. The goal is to be fast and memory efficient.

## Description

RML (RDF Mapping Language) is central to knowledge acquisition. FlexRML is a flexible RML processor able to run on a wide range of devices:

- Cloud Environments
- Consumer Hardware
- Single Board Computers
- Microcontrollers (Separate Repository)

Currently, FlexRML supports data in CSV and JSON format.

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
- `serd`
- `xxhash`

Install vcpkg, make sure `vcpkg` is on your `PATH`, then install dependencies from the project root:

```bash
vcpkg install
```

The CMake build detects dependencies in `vcpkg_installed/<triplet>`. For a classic vcpkg install, set `VCPKG_ROOT` before building:

```bash
export VCPKG_ROOT=$HOME/vcpkg
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

The executable links FlexRML code, Serd, jsoncons, and xxHash into one binary; it still depends on normal system runtime libraries such as `libstdc++` and `libc`.

### Versioning

The project version is set in `CMakeLists.txt`:

```cmake
project(flexrml VERSION 2.2.0 LANGUAGES CXX)
```

CMake generates the runtime version header from that value. Check the built executable with:

```bash
./flexrml --version
```

## Getting Started

To execute a mapping use: 

```bash
./flexrml -m [path]
```

More information about available flags can be found using the `-h` flag.

## Conformance

FlexRML passes all official [`RML-Core test cases`](https://github.com/kg-construct/rml-core/tree/main/test-cases) and all official [`RML-FNML test cases`](https://github.com/kg-construct/rml-fnml/tree/main/test-cases).

The runtime is C++; Python is only used for validation tooling. To run conformance validation, install the Python test dependency:

```bash
python3 -m venv env
source env/bin/activate
pip install -r requirements.txt
```

Place the official test cases in `test_cases/`. Category subfolders such as
`test_cases/rml-core/` are supported. Build and run validation through CMake with:

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
- [xxHash](https://github.com/Cyan4973/xxHash) is licensed under the BSD 2-Clause License.
