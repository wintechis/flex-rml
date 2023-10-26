# FlexRML - A Flexible RML Processor

FlexRML provides a robust RML processing solution tailored for different devices. Whether you're working with microcontrollers, single-board computers, consumer hardware, or cloud environments, FlexRML ensures seamless integration and efficient processing.

## Description

RML (RDF Mapping Language) is central to data transformation and knowledge graph construction. FlexRML is a flexible RML processor optimized for a wide range of devices:

- Microcontrollers
- Single Board Computers
- Consumer Hardware
- Cloud Environments

## Installation
**Prerequisites**

Before compilation, a build environment must be set up on the system.

This can be done on Debian based systems via :
```bash
apt install build-essential
```

**Compiling from Source:**

1. Clone or download the repository.
2. Navigate to the project directory.
3. Execute the make file with the command: `make`
4. After compilation, you'll find the executable named `FlexRML`.

## Usage

FlexRML operates as a command-line program, offering a variety of configuration flags for ease of use:

```bash
./FlexRML [OPTIONS]
```
- -m [path] Specify the path to the mapping file.
- -o [name] Define the name for the output file.

For example:
```bash
./FlexRML -m ./path/to/mapping_file.ttl -o output_file.nq
```


## Example
In the example folder, there is a mapping.ttl file that contains RML rules for mapping the data to RDF, and a sensor_values.csv file.

The sensor_values.csv contains:

| id  | name    | value | unit |
| --- | ------- | ----- | ---- |
| 10  | Sensor1 | 24    | C    |
| 20  | Sensor2 | 72.2  | F    |
| 30  | Sensor3 | 34    | C    |

If you are in the example folder and run:
```bash
./FlexRML -m ./mapping.ttl -o output_file.nq
```
The resulting RDF graph can be found in output_file.nq.