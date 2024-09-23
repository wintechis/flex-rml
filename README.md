# FlexRML - A Flexible RML Processor

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.10256148.svg)](https://doi.org/10.5281/zenodo.10256148)

FlexRML provides a robust RML processing solution tailored for different devices. Whether you're working with microcontrollers, single-board computers, consumer hardware, or cloud environments, FlexRML ensures seamless integration and efficient processing.

## Description

RML (RDF Mapping Language) is central to data transformation and knowledge graph construction. FlexRML is a flexible RML processor optimized for a wide range of devices:

- Microcontrollers
- Single Board Computers
- Consumer Hardware
- Cloud Environments

Currently, FlexRML only supports data in CSV format. However, future versions will include support for additional data formats such as JSON and XML.

## Installation

### Using Prebuilt Binaries
Prebuilt binaries for various systems are available in the [releases](https://github.com/wintechis/flex-rml/releases) section.

### Compiling from Source
**Prerequisites**

Before compilation, set up a build environment on your system. 
On Debian-based systems, this can be done using:

```bash
apt install build-essential cmake git curl zip unzip tar
```
Additionally, ensure that you have `vcpkg` installed as it will be used for managing dependencies.

**Compilation Process:**

1. Clone or download the repository.
   Clone or download the repository from GitHub and navigate to the project directory.
```bash
git clone git@github.com:wintechis/flex-rml.git
cd flexrml
```
2. Install `vcpkg` as package manager.
   If you haven't installed `vcpkg`, clone it from GitHub and bootstrap it.
```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
```
4. Configure the project with CMake
   Use CMake to configure the project, specifying the vcpkg toolchain file and the paths to dependencies if necessary.
   **Note: You need to adjust the path to vcpkg.**
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```
5. Compile the project
```bash
cmake --build build
```
6. After compilation, the executable `flexrml` will be available in the `build` directory. You can run it using:

**Troubleshooting**
- If you encounter errors during the CMake configuration, ensure the paths to serd and cityhash are correctly specified.
- Make sure your system has the correct C++ compiler installed (GCC or Clang).
- Clean the build directory if you face repeated configuration issues:
```bash
rm -rf build/*
```

## Getting Started

Depending on the use case and environment FlexRML is executed, different configurations are usefull. 

**Fastest Execution Speed**

The method prioritizes faster execution speed at the expense of increased memory usage. It uses a 128-bit hash function to identify duplicates and bypasses the result size estimation step to achieve faster performance. To use this mode, run the following command:
```bash
./flexrml -m [path] -d -t
```


**Lowest memory consumption**

This mode is optimized for minimal memory usage by using a result size estimator to approximate the number of N-Quads generated. Although this process takes more time due to the additional computation, it conserves memory, in particular when the estimated number of N-Quads is less than 135,835,773. This approach is beneficial in memory-constrained environments. To enable this mode, use the following command:
```bash
./flexrml -m [path] -d -t -a
```

More informatioin about available flags can be found on the [wiki](https://github.com/wintechis/flex-rml/wiki/How-To-Use%3F).

## Example

In the example folder, there is a mapping.ttl file that contains RML rules for mapping sensor data to RDF, and a sensor_values.csv file.

The sensor_values.csv contains:

| id  | name    | value | unit |
| --- | ------- | ----- | ---- |
| 10  | Sensor1 | 24    | C    |
| 20  | Sensor2 | 72.2  | F    |
| 30  | Sensor3 | 34    | C    |

If you are in the example folder and run:

```bash
./flexrml -m ./mapping.ttl -o output_file.nq -d
```

The resulting RDF graph can be found in output_file.nq.
The graph looks like this:
![Resulting_Graph](https://github.com/FreuMi/FlexRML/blob/main/example/output_graph.png)

## Conformance

FlexRML is validated against applicable RML test cases to ensure conformance with the specification. <br>
Currently, only **CSV-related** test cases are applicable.

| Specification                                                                 | Coverage               |
|------------------------------------------------------------------------------|------------------------|
| [RML-Core](https://github.com/kg-construct/rml-core/tree/main/test-cases)    | 100% Coverage          |
| [RML-IO](https://github.com/kg-construct/rml-io/tree/main/test-cases)        | Work in Progress |
| [RML-CC](https://github.com/kg-construct/rml-cc/tree/main/test-cases)        | Work in Progress  |



## Planned Features for FlexRML
We are constantly working to improve FlexRML and expand its capabilities. Here's what we have planned for the future development of FlexRML:
- [ ] **Add Support for Other Data Encodings** Enhancing FlexRML to work with various data formats.
     + [ ] JSON
          - [x] Add JSON reader and JSON Path parser
          - [ ] Adjust generation of index for hash join to JSON
          - [ ] Adjust result size estimation to JSON
     + [ ] XML
- [x] **Add Support for N-Triple RDF Serialization** Implementing N-Triple format compatibility for broader RDF serialization options.
- [ ] **Improve Performance of Join Algorithm** Optimize the current join algorithm for faster and more efficient data processing.
- [ ] **Provide Library for Arduinos** Develop a specialized library to make FlexRML easier useable on Arduino devices, expanding its use in IoT applications.
- [ ] **Support latest RML vocabulary** Modify the parsing of RML rules to allow the new RML vocabulary to be used.

We welcome community feedback and contributions! If you have suggestions or want to contribute to any of these features, please let us know through GitHub issues.

## ESP32 Compatible Version

For those working with ESP32, we have a dedicated version of this project. It's tailored specifically for compatibility with ESP32 and the Arduino IDE. You can access it and find detailed instructions for setup and use at the following link:
[FlexRML ESP32 Repository](https://github.com/wintechis/flex-rml-esp32/tree/main)

## JavaScript Compatible Version
For those working with JavaScript, we have created a [Webassembly version of FlexRML](https://github.com/wintechis/flex-rml-node/). FlexRML-node is published on [npm](https://www.npmjs.com/package/flexrml-node).

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

### External Libraries
This project uses external libraries:

- [Serd](https://github.com/drobilla/serd) is licensed under the ISC License.
- [CityHash](https://github.com/google/cityhash/) is licensed under the MIT License.
- [AdrduinoJson](https://github.com/bblanchon/ArduinoJson) is licensed under the MIT License.
