import ctypes
import os
import argparse
import sys
import time

from .backend.backend import run_converter

#############################################
## DEFAULT VALUES
BASE_URI = "http://example.com/base/"

#############################################

class Configuration:
    def __init__(self):
        self.mapping_path_file = ""
        self.output_file_path = ""
        self.plan = ""
        self.base_uri = BASE_URI
        self.continue_on_error = "false"
        self.threading_enabled = "true"
        self.materialize_constants = "true"
        self.heuristic_ordering = "true"
        self.generate_plan = True

        ##########################
        ## Internal Config
        # Used to show output or not
        self.show_output = False

        # If True returns triple else prints
        # Used in combination with 
        self.return_triple = True 
        ##########################
        self.version = "2.0.0"
        self.bn_number = 58932
        
        self.lib_rml_parser = None
        self.lib_rml_io_normalizer = None
        self.lib_ra_converter = None
        self.lib_rml_functions = None

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

    def load_rml_functions(self):
        base_path = os.path.dirname(__file__)
        lib_path = os.path.join(base_path, "frontend", "libfunctionexecutor.so")

        try:
            lib = ctypes.CDLL(lib_path)
            lib.resolve_rml_functions.argtypes = [ctypes.c_char_p]
            lib.resolve_rml_functions.restype = ctypes.c_char_p
            return lib
        except OSError as e:
            print(f"Error loading './frontend/libfunctionexecutor.so': {e}")
            sys.exit(1)

####################################################################################################################

def load_rml(file_path, config):
    if config.lib_rml_parser == None:
        config.lib_rml_parser = config.load_rml_parser()

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
    if config.lib_rml_io_normalizer == None:
        config.lib_rml_io_normalizer = config.load_rml_io_normalizer()

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
    if config.lib_ra_converter == None:
        config.lib_ra_converter = config.load_ra_converter()

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
### Handle Functions
def handle_functions(normalized_graphs_arr, config):
    if config.lib_rml_functions == None:
        config.lib_rml_functions = config.load_rml_functions()

    lib = config.lib_rml_functions

    input_str = "===".join(normalized_graphs_arr).encode("utf-8")

    # Call Cpp
    results = lib.resolve_rml_functions(input_str)
    results = results.decode()

    # Split back to graphs
    graph_str = results.strip().split("===")

    return graph_str


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

####################################################################################################################
#### EXECUTION
####################################################################################################################

def run_mapping(mapping_config):
    if mapping_config.plan == "":
        ############ Generate Plan and Execute ############
        ### STEP 1: Parse & Validate ###
        load_rml_start_time = time.time()
        rml_str = load_rml(mapping_config.mapping_file_path, mapping_config)
        if mapping_config.show_output:
            print("RML loading: ", time.time()-load_rml_start_time)


        ### STEP 2: Rewrite & Normalize ###
        normalization_start_time = time.time()
        normalized_graphs_arr = normalize_mapping(rml_str, mapping_config)
        normalized_graphs_arr.sort()
        if mapping_config.show_output:
            print("Normalizing: ", time.time()-normalization_start_time)

        ### STEP 3: Handle Functions
        handle_functions_start_time = time.time()
        normalized_graphs_arr = handle_functions(normalized_graphs_arr, mapping_config)
        if mapping_config.show_output:
            print("Calling functions: ", time.time()-handle_functions_start_time)

        iterators = get_iterators(normalized_graphs_arr)
        
        ### STEP 4: Logical plan generation ###
        convert_to_ra_start_time = time.time()
        ra_expressions, ra_expressions_iterators = convert_to_ra(normalized_graphs_arr, iterators, mapping_config)
        ra_str = to_ra_string(ra_expressions)
        if mapping_config.show_output:
            print("Converting to RA: ", time.time()-convert_to_ra_start_time)

        ### Check if JSON and remove "$"
        ra_str = ra_str.replace("$.","")  
        ra_str = ra_str.replace("['","") 
        ra_str = ra_str.replace("']","")  
        
        # just return the plan
        if mapping_config.generate_plan:
            print(f"{ra_str}<==>{ra_expressions_iterators}")
        else:
            triple = run_converter(ra_str, mapping_config.output_file_path, mapping_config.base_uri, mapping_config.continue_on_error, mapping_config.threading_enabled, 
                        mapping_config.materialize_constants, mapping_config.heuristic_ordering, mapping_config.return_triple, ra_expressions_iterators)
            
            return triple
    else:
        # Just execute
        import ast
        ra_str = mapping_config.plan.split("<==>")[0]
        ra_expressions_iterators = mapping_config.plan.split("<==>")[1]
        ra_expressions_iterators = ast.literal_eval(ra_expressions_iterators)
        triple = run_converter(ra_str, mapping_config.output_file_path, mapping_config.base_uri, mapping_config.continue_on_error, mapping_config.threading_enabled, 
                        mapping_config.materialize_constants, mapping_config.heuristic_ordering, mapping_config.return_triple, ra_expressions_iterators)
        return triple

####################################################################################################################
# Function to use as library
def execute(mapping_file_path, base_uri = BASE_URI, generate_plan = False):
    config = Configuration()
    config.mapping_file_path = mapping_file_path
    config.generate_plan = generate_plan
    config.base_uri = base_uri

    triple = run_mapping(config)

    return triple

####################################################################################################################
# Function executed if used form CLI
def main():
    ### Handle CLI input ###
    config = Configuration()
    
    parser = argparse.ArgumentParser(description="flexrml: An experimental, really fast RML interpreter. Note: stability not guaranteed.")
    parser.add_argument("-m", "--mapping", type=str, required=False, help="The path to the RML mapping file")
    parser.add_argument("-o", "--output", type=str, required=False, help="The path where the output RDF graph is stored.")
    parser.add_argument("-b", "--base", type=str, required=False, help="The base URI used to generate RDF terms.")
    parser.add_argument("-v", "--version", action='store_true', help="Displays the version of this FlexRML build.")
    parser.add_argument("-gp", "--generate-plan", action='store_true', help="Return only plan.")
    parser.add_argument("-p", "--plan", type=str, required=False, help="Execute mapping form existing plan.")
    parser.add_argument("--continue-on-error", action='store_true', help="Continues on error if the flag is set.")
    parser.add_argument("--no-threading", action='store_false', help="Disables multithreading during execution.")
    parser.add_argument("--no-const-folding", action='store_false', help="Disables constant folding optimization.")
    parser.add_argument("--no-ordering", action='store_false', help="Disables heuristic ordering optimization.")

    args = parser.parse_args()

    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(0) 

    if args.version:
        VERSION_TYPE = "Dev"
        print(f"flexrml {config.version} {VERSION_TYPE} - experimental. really fast. stability not guaranteed.")
        sys.exit(0)

    if args.mapping:
        config.mapping_file_path = args.mapping
    elif args.plan:
        config.plan = args.plan
    else:
        print("Error: No mapping file or plan provided. Nothing to do.\n")
        parser.print_help()
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

    if args.generate_plan == False:
        config.generate_plan = False

    ### Execute ###
    run_mapping(config)

        
if __name__ == "__main__":
    #start_time = time.time()
    main()
    #print("Total time:", time.time() - start_time)