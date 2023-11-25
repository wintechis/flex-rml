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

FlexRML operates as a command-line program, offering a variety of configuration flags for ease of use:

```bash
./FlexRML [OPTIONS]
```

| Flag  | Description    | Default |
| --- | ------- | ----- | 
| -h | Display available flags | -- |
| -m [path] | Specify the path to the mapping file. | --  | 
| -o [name]  | Define the name for the output file. | "output.nq"  | 
| -d   | Remove duplicate entries before writing to the output file. | false | 
| -t   | Use threading, by default the maximum number of available threads are used. | false | 
| -tc [integer]   | Specify the number of threads to use. If set to 0 all threads are used. | 0 | 
| -a   | Use adaptive Result Size Estimation and adaptive hash size selection. | 0 | 
| -p [float]   | Set sampling probability used for Result Size Estimation. Higher probabities produce better estimates but need more time. | 0.2 | 
| -c [path]   | Use config file instead of command line arguments. | -- | 
| -b [integer]   | Use a fixed hash size, value which must be one of [32, 64, 128] | -- |
| -r [tokens,to,remove]   | ist of tokens which should be removed from the input. | -- | 


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

## Suggested Configurations
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
