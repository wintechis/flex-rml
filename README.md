# FlexRML - A Flexible RML Processor

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
Prebuilt binaries for various systems are available in the releases section.

### Compiling from Source
**Prerequisites**

Before compilation, set up a build environment on your system. 
On Debian-based systems, this can be done using:

```bash
apt install build-essential
```

**Compilation Process:**

1. Clone or download the repository.
2. Navigate to the project directory.
3. Run the makefile using the command: `make`.
4. After compilation, the executable `FlexRML` will be available in the directory.

## Getting Started

Depending on the use case and environment FlexRML is executed, different configurations are usefull. 

**Fastest Execution Speed**

The method prioritizes faster execution speed at the expense of increased memory usage. It uses a 128-bit hash function to identify duplicates and bypasses the result size estimation step to achieve faster performance. To use this mode, run the following command:
```bash
./FlexRML -m [path] -d -t
```


**Lowest memory consumption**

This mode is optimized for minimal memory usage by using a result size estimator to approximate the number of N-Quads generated. Although this process takes more time due to the additional computation, it conserves memory, in particular when the estimated number of N-Quads is less than 135,835,773. This approach is beneficial in memory-constrained environments. To enable this mode, use the following command:
```bash
./FlexRML -m [path] -d -t -a
```

More informatioin about available flags can be found on the [wiki](https://github.com/wintechis/FlexRML/wiki/cli_parameters).

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
./FlexRML -m ./mapping.ttl -o output_file.nq -d
./FlexRML -m ./mapping.ttl -o output_file.nq -d
```

The resulting RDF graph can be found in output_file.nq.
The graph looks like this:
![Resulting_Graph](https://github.com/FreuMi/FlexRML/blob/main/example/output_graph.png)

## Conformance
FlexRML is validated against the latest applicable [RML test cases](https://github.com/kg-construct/rml-test-cases) to ensure conformance with the specification.

## Planned Features for FlexRML
We are constantly working to improve FlexRML and expand its capabilities. Here's what we have planned for the future development of FlexRML:
- [ ] **Add Support for Other Data Encodings** Enhancing FlexRML to work with various data formats.
     + [ ] JSON
     + [ ] XML
- [ ] **Add Support for N-Triple RDF Serialization** Implementing N-Triple format compatibility for broader RDF serialization options.
- [ ] **Explore WASM (WebAssembly)** Investigate the possibility of running FlexRML in web browsers.
- [ ] **Improve Performance of Join Algorithm** Optimize the current join algorithm for faster and more efficient data processing.
- [ ] **Provide Library for Arduinos** Develop a specialized library to make FlexRML easier useable on Arduino devices, expanding its use in IoT applications.

We welcome community feedback and contributions! If you have suggestions or want to contribute to any of these features, please let us know through GitHub issues.
