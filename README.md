# FlexRML - experimental. really fast. stability not guaranteed.

FlexRML is an experimental RML processor mostly written in C++ tied together with Python. The goal is to be fast and memory efficient.

## Description

RML (RDF Mapping Language) is central to knowledge aquisition. FlexRML is a flexible RML processor able to run on a wide range of devices:

- Cloud Environments
- Consumer Hardware
- Single Board Computers
- Microcontrollers (Separate Repository)

Currently, FlexRML supports data in CSV and JSON format.

## Installation

### Using Prebuilt Binaries
Prebuilt binaries for Debian based systems are available in the [releases](https://github.com/wintechis/flex-rml/releases) section. 

### Compiling from Source or use Python Interpreter

**Prerequisites**
We test on Ubuntu 24.04 LTS with Pyhton 3.12 and gcc 13.3.

Install C++ compiler: 
```bash
sudo apt install build-essential
```

**Compilation Process:**

1. Clone or download the repository.
   Clone or download the repository from GitHub and navigate to the project directory.
```bash
git clone git@github.com:wintechis/flex-rml.git
cd flexrml
```
2. Setup Python3 venv.
```bash
python3 -m venv venv
source venv/bin/activate
```

#### For Python Interpreter
3. Install Python dependencies.
```bash
pip install -r requirements.txt
```

4. Execute the build script.
```bash
./build.sh
```
5. After compilation, you can run `python3 flexrml.py`.

#### Standalone
3. Install Pyhton build dependcies.
```bash
pip install -r requirements_build.txt
```
4. Execute the build script.
```bash
./build_standalone.sh
```
5. After compilation, you find the standalone `flexrml`.

Note: The standalone version typically requieres more memory and is a bit slower executing mappings.

## Getting Started

To execute a mapping use: 

```bash
./flexrml -m [path]
```

```bash
python3 flexrml.py -m [path]
```

More informatioin about available flags can be found using the `-h` flag.

## Conformance
#TODO

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
This project uses two external C++ libraries:

- [Serd](https://github.com/drobilla/serd) is licensed under the ISC License.
- [xxHash](github.com/Cyan4973/xxHash) is licensed under the BSD 2-Clause License.
