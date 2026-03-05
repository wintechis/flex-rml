import ctypes
import os
import argparse
import sys
import time
import re

from backend.backend import run_converter

class Configuration:
    def __init__(self):
        self.mapping_path_file = ""
        self.output_file_path = ""
        self.base_uri = "http://example.com/base/"
        self.continue_on_error = "false"
        self.threading_enabled = "true"
        self.materialize_constants = "true"
        self.heuristic_ordering = "true"
        self.version = "2.0.0"
        self.bn_number = 58932
        self.show_output = False
        self.lib_rml_parser = self.load_rml_parser()
        self.lib_rml_io_normalizer = self.load_rml_io_normalizer()
        self.lib_ra_converter = self.load_ra_converter()

    def load_rml_parser(self):
        base_path = os.path.dirname(__file__)
        lib_path = os.path.join(base_path, "frontend", "librdfparser.so")

        try:
            lib = ctypes.CDLL(lib_path)
            lib.parse_rdf.argtypes = [ctypes.c_char_p]
            lib.parse_rdf.restype = ctypes.c_char_p
            return lib
        except OSError as e:
            print(f"Error loading './frontend/librdfparser.so': {e}")
            sys.exit(1)

    def load_rml_io_normalizer(self):
        base_path = os.path.dirname(__file__) 
        lib_path = os.path.join(base_path, "frontend", "libnormalizer.so")

        try:
            lib = ctypes.CDLL(lib_path)
            lib.normalize_rml_mapping.argtypes = [ctypes.c_char_p, ctypes.c_int]
            lib.normalize_rml_mapping.restype = ctypes.c_char_p
            return lib
        except OSError as e:
            print(f"Error loading './frontend/libnormalizer.so': {e}")
            sys.exit(1)

    def load_ra_converter(self):
        base_path = os.path.dirname(__file__)
        lib_path = os.path.join(base_path, "frontend", "libraconverter.so")

        try:
            lib = ctypes.CDLL(lib_path)
            lib.create_relational_algebra.argtypes = [ctypes.c_char_p]
            lib.create_relational_algebra.restype = ctypes.c_char_p
            return lib
        except OSError as e:
            print(f"Error loading './frontend/libraconverter.so': {e}")
            sys.exit(1)

####################################################################################################################

def load_rml(file_path, config):
    try:
        with open(file_path, "r") as f:
            raw_mapping = f.read()

        raw_mapping = raw_mapping.encode()
        lib = config.lib_rml_parser
        result = lib.parse_rdf(raw_mapping)
        if result is None:
            print("RDF parser function returned NULL or encountered an error")
            sys.exit(1)

        result_str = result.decode()
        if result_str.startswith("Error:"):
            print("RML PARSING:", result_str)
            sys.exit(1)

        return result_str
    except OSError as e:
        print(f"Failed to load library: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)

def normalize_mapping(rml_str, config):
    rml_str = rml_str.encode()

    lib = config.lib_rml_io_normalizer
    result = lib.normalize_rml_mapping(rml_str, config.bn_number)

    if not result:
        print("Error: Function returned NULL")
        sys.exit(1)

    normalized_result = result.decode()

    # Separate graphs
    normalized_graphs = []
    for sub_graph in normalized_result.split("===="):
        sub_graph = sub_graph.strip()
        if sub_graph == "":
            continue
        normalized_graphs.append(sub_graph)

    return normalized_graphs

def convert_to_ra(normalized_graphs_arr, iterators, config):
    ra_expressions = []
    ra_expressions_iterators = []
    for i in range(len(normalized_graphs_arr)):
        normalized_graph = normalized_graphs_arr[i]
        iterator = iterators[i] # Link iterator and ra_expression
        normalized_graph = normalized_graph.encode()

        lib = config.lib_ra_converter
        results = lib.create_relational_algebra(normalized_graph)

        results = results.decode()

        results_split = results.strip().split("\n")
        results_split = [item for item in results_split if item] # Remove empty strings ""

        for result in results_split:
            ra_expressions.append(result)
            ra_expressions_iterators.append(iterator)

    return ra_expressions, ra_expressions_iterators

####################################################################################################################

def handle_cli(config):
    parser = argparse.ArgumentParser(description="flexrml: An experimental, really fast RML interpreter. Note: stability not guaranteed.")
    parser.add_argument("-m", "--mapping", type=str, required=False, help="The path to the RML mapping file")
    parser.add_argument("-o", "--output", type=str, required=False, help="The path where the output RDF graph is stored.")
    parser.add_argument("-b", "--base", type=str, required=False, help="The base URI used to generate RDF terms.")
    parser.add_argument("-v", "--version", action='store_true', help="Displays the version of this FlexRML build.")
    parser.add_argument("--continue-on-error", action='store_true', help="Continues on error if the flag is set.")
    parser.add_argument("--no-threading", action='store_false', help="Disables multithreading during execution.")
    parser.add_argument("--no-const-folding", action='store_false', help="Disables constant folding optimization.")
    parser.add_argument("--no-ordering", action='store_false', help="Disables heuristic ordering optimization.")


    args = parser.parse_args()

    if args.version:
        print(f"flexrml {config.version} - experimental. really fast. stability not guaranteed.")
        sys.exit(0)

    if args.mapping:
        config.mapping_file_path = args.mapping
    else:
        print("No mapping file provided. Use -m flag. Nothing to do.")
        sys.exit(0)


    if args.output:
        config.output_file_path = args.output

    if args.base:
        config.base_uri = args.base

    if args.continue_on_error:
        config.continue_on_error = str(args.continue_on_error).lower()

    if args.no_threading == False:
        config.threading_enabled = str(args.no_threading).lower()
    
    if args.no_const_folding == False:
        config.materialize_constants = str(args.no_const_folding).lower()

    if args.no_ordering == False:
        config.heuristic_ordering = str(args.no_const_folding).lower()


####################################################################################################################

def to_ra_string(ra_expressions):
    res = ""
    for ra_expression in ra_expressions:
        res += ra_expression + "\n"

    res.strip()
    return res

def get_iterators(normalized_graphs_arr):
    iterators = []
    for normalized_graph in normalized_graphs_arr:
        path = None
        iterator = None
        tmp_iterators = {}
        split_graph = normalized_graph.split("\n")
        for triple_str in split_graph:
            if "|||http://w3id.org/rml/iterator|||" in triple_str:
                triple = triple_str.split("|||")
                iterator = triple[2]
                logical_source_node = triple[0]

                for triple_str2 in split_graph:
                    if f"{logical_source_node}|||http://w3id.org/rml/source|||" in triple_str2:
                        triple = triple_str2.split("|||")
                        source_node = triple[2]

                        for triple_str3 in split_graph:
                            if f"{source_node}|||http://w3id.org/rml/path|||" in triple_str3:
                                triple = triple_str3.split("|||")
                                path = triple[2]
                tmp_iterators[path] = iterator
        iterators.append(tmp_iterators)

    return iterators


def simpleRML():
    pass

def main():
    start_time = time.time()

    ### Handle CLI input ###
    config = Configuration()
    handle_cli(config)

    ### STEP 1: Parse & Validate ###
    rml_str = load_rml(config.mapping_file_path, config)

    ### STEP 2: Rewrite & Normalize ###
    normalized_graphs_arr = normalize_mapping(rml_str, config)
    normalized_graphs_arr.sort()

    iterators = get_iterators(normalized_graphs_arr)
    
    ### STEP 3: Logical plan generation ###
    ra_expressions, ra_expressions_iterators = convert_to_ra(normalized_graphs_arr, iterators, config)

    ra_str = to_ra_string(ra_expressions)

    ### Check if JSON and remove "$"
    ra_str = ra_str.replace("$.","")  
    ra_str = ra_str.replace("['","") 
    ra_str = ra_str.replace("']","")  

    if config.show_output:
        print("Frontend took:", time.time()-start_time)

    run_converter(ra_str, config.output_file_path, config.base_uri, config.continue_on_error, config.threading_enabled, 
                  config.materialize_constants, config.heuristic_ordering, ra_expressions_iterators)

if __name__ == "__main__":
    main()