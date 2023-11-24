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

## Usage

FlexRML operates as a command-line program, offering a variety of configuration flags for ease of use:

```bash
./FlexRML [OPTIONS]
```

- -m [path] Specify the path to the mapping file.
- -o [name] Define the name for the output file.
- -d Remove duplicate entries before writing to the output file.
- -t Use threading, by default the maximum number of available threads are used.
- -tc [integer] Specify the number of threads that should be used.
- -a Use adaptive Result Size Estimation and adaptive hash size selection
- -p [float] Set sampling probability used for Result Size Estimation. Higher probabities produce better estimates but need more time.
- -c [path] Use config file instead of command line arguments.
- -b [integer] Use a fixed hash size, value which must be one of [32, 64, 128]

**Note:**

- When a config file is specified using the '-c' flag, all other command-line arguments are ignored, and settings are exclusively loaded from the config file.
- Selecting a fixed hash size using the '-b' flag skips the adaptive Result Size Estimation. Be aware that if the manually chosen hash size is too small for the input data, hash collisions may occur. This can lead to missing N-Quads in the output.

For example:

```bash
./FlexRML -m ./path/to/mapping_file.ttl -o output_file.nq -d
```

or when using a config file equivalent to the command above:

```bash
./FlexRML -c ./path/to/config_file.ini
```

```python
# Config File
#
# I/O Settings
mapping=./path/to/mapping_file.ttl
output_file=output_file.nq

# Mapping Settings
remove_duplicates=true
use_threading=false
number_of_threads=4
fixed_hash_size=
adaptive_hash_selection=false
sampling_probability=0.1
```

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
