import ctypes
import os
import argparse
import re
import sys
import time
from pathlib import Path

from flexrml.backend.backend import run_converter

#############################################
## DEFAULT VALUES
BASE_URI = "http://example.com/base/"
SHACL_CORE_SHAPE = None

#############################################

def package_root() -> Path:
    here = Path(__file__).resolve().parent
    if here.name == "flexrml":
        return here
    return here / "flexrml"

SHACL_CORE_SHAPE = package_root() / "shapes" / "core.ttl"

def resource_path(*parts: str) -> str:
    return str(package_root().joinpath(*parts))

class Configuration:
    def __init__(self):
        self.mapping_source = ""
        self.output_file_path = ""
        self.plan = ""
        self.base_uri = BASE_URI
        self.continue_on_error = "false"
        self.threading_enabled = "true"
        self.materialize_constants = "true"
        self.heuristic_ordering = "true"
        self.generate_plan = True
        self.data = None
        self.validate_shacl = False

        ##########################
        ## Internal Config
        self.show_output = False
        self.return_triple = True
        ##########################

        self.version = "2.3.0"
        self.type = ""
        self.bn_number = 58932

        self.lib_rml_parser = None
        self.lib_rml_io_normalizer = None
        self.lib_ra_converter = None
        self.lib_rml_functions = None

    def _load_cdll(self, relative_path: str) -> ctypes.CDLL:
        lib_path = resource_path(*relative_path.split("/"))

        try:
            return ctypes.CDLL(lib_path)
        except OSError as e:
            print(f"Error loading '{lib_path}': {e}")
            sys.exit(1)

    def load_rml_parser(self):
        lib = self._load_cdll("frontend/librdfparser.so")
        lib.parse_rdf.argtypes = [ctypes.c_char_p]
        lib.parse_rdf.restype = ctypes.c_char_p
        return lib

    def load_rml_io_normalizer(self):
        lib = self._load_cdll("frontend/libnormalizer.so")
        lib.normalize_rml_mapping.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.normalize_rml_mapping.restype = ctypes.c_char_p
        return lib

    def load_ra_converter(self):
        lib = self._load_cdll("frontend/libraconverter.so")
        lib.create_relational_algebra.argtypes = [ctypes.c_char_p]
        lib.create_relational_algebra.restype = ctypes.c_char_p
        return lib

    def load_rml_functions(self):
        lib = self._load_cdll("frontend/libfunctionexecutor.so")
        lib.resolve_rml_functions.argtypes = [ctypes.c_char_p]
        lib.resolve_rml_functions.restype = ctypes.c_char_p
        return lib


def _import_validation_dependencies():
    try:
        from pyshacl import validate
        from rdflib import Graph
        from rdflib.util import guess_format
    except ImportError as exc:
        print(
            "SHACL validation requires the optional dependencies 'pyshacl' and 'rdflib'. "
            "Install the current project dependencies and retry."
        )
        sys.exit(1)

    return validate, Graph, guess_format


def _parse_graph(source):
    _, Graph, guess_format = _import_validation_dependencies()

    parse_attempts = []

    try:
        is_existing_file = isinstance(source, (str, os.PathLike)) and Path(source).is_file()
    except OSError:
        is_existing_file = False

    if is_existing_file:
        source_path = Path(source)
        guessed_format = guess_format(source_path.name)
        if guessed_format:
            parse_attempts.append({"location": str(source_path), "format": guessed_format})

        for fallback_format in ("turtle", "trig", "n3", "nt", "xml"):
            if fallback_format != guessed_format:
                parse_attempts.append({"location": str(source_path), "format": fallback_format})
    else:
        for fallback_format in ("turtle", "trig", "n3", "nt", "xml"):
            parse_attempts.append({"data": source, "format": fallback_format})

    last_error = None
    for parse_kwargs in parse_attempts:
        graph = Graph()
        try:
            graph.parse(**parse_kwargs)
            return graph
        except Exception as exc:
            last_error = exc

    raise ValueError(f"Unable to parse the input mapping as RDF: {last_error}")


def validate_rml_with_shacl(source):
    validate, _, _ = _import_validation_dependencies()

    try:
        if not SHACL_CORE_SHAPE.is_file():
            raise FileNotFoundError(f"Bundled SHACL shape not found: {SHACL_CORE_SHAPE}")

        data_graph = _parse_graph(source)
        conforms, _, results_text = validate(
            data_graph=data_graph,
            shacl_graph=str(SHACL_CORE_SHAPE),
            inference="none",
            abort_on_first=False,
            allow_infos=True,
            allow_warnings=True,
        )
    except Exception as exc:
        print(f"SHACL validation failed before execution: {exc}")
        sys.exit(1)

    if not conforms:
        print("RML SHACL validation failed against the official RML-Core shape.")
        print(results_text)
        sys.exit(1)

####################################################################################################################

def load_rml(source, config):
    if config.lib_rml_parser == None:
        config.lib_rml_parser = config.load_rml_parser()

    try:
        try:
            is_existing_file = isinstance(source, (str, os.PathLike)) and Path(source).is_file()
        except OSError:
            is_existing_file = False

        if is_existing_file:
            with open(source, "r", encoding="utf-8") as f:
                raw_mapping = f.read()
        else:
            raw_mapping = source

        raw_mapping = raw_mapping.encode()
        lib = config.lib_rml_parser
        result = lib.parse_rdf(raw_mapping)
        if result is None:
            print("RDF parser function returned NULL or encountered an error")
            sys.exit(1)

        result_str = result.decode()
        if result_str.startswith("Error:"):
            print("RML PARSING:", result_str)
            print("Check syntax of mapping file.")
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

def convert_to_ra(normalized_graphs_arr, iterators, base_uris, config):
    if config.lib_ra_converter == None:
        config.lib_ra_converter = config.load_ra_converter()

    ra_expressions = []
    ra_expressions_iterators = []
    ra_expressions_base_uris = []
    for i in range(len(normalized_graphs_arr)):
        normalized_graph = normalized_graphs_arr[i]
        iterator = iterators[i] # Link iterator and ra_expression
        base_uri = base_uris[i]
        normalized_graph = normalized_graph.encode()

        lib = config.lib_ra_converter
        results = lib.create_relational_algebra(normalized_graph)

        results = results.decode()

        results_split = results.strip().split("\n")
        results_split = [item for item in results_split if item] # Remove empty strings ""

        for result in results_split:
            ra_expressions.append(result)
            ra_expressions_iterators.append(iterator)
            ra_expressions_base_uris.append(base_uri)

    return ra_expressions, ra_expressions_iterators, ra_expressions_base_uris

def get_base_uris(normalized_graphs_arr, default_base_uri):
    base_uris = []
    for normalized_graph in normalized_graphs_arr:
        triples_map = None
        graph_base_uri = default_base_uri
        split_graph = normalized_graph.split("\n")

        for triple_str in split_graph:
            triple = triple_str.split("|||")
            if len(triple) == 3 and triple[1] == "http://www.w3.org/1999/02/22-rdf-syntax-ns#type" and triple[2] == "http://w3id.org/rml/TriplesMap":
                triples_map = triple[0]
                break

        if triples_map is not None:
            for triple_str in split_graph:
                triple = triple_str.split("|||")
                if len(triple) == 3 and triple[0] == triples_map and triple[1] == "http://w3id.org/rml/baseIRI":
                    graph_base_uri = triple[2]
                    break

        base_uris.append(graph_base_uri)

    return base_uris

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
    if results.strip() == "":
        print("Error: Function resolution failed.")
        sys.exit(1)

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
        reference_formulation = None
        tmp_iterators = {}
        split_graph = normalized_graph.split("\n")
        for triple_str in split_graph:
            if "|||http://w3id.org/rml/iterator|||" in triple_str:
                triple = triple_str.split("|||")
                iterator = triple[2]
                logical_source_node = triple[0]

                for triple_str2 in split_graph:
                    if f"{logical_source_node}|||http://w3id.org/rml/referenceFormulation|||" in triple_str2:
                        triple = triple_str2.split("|||")
                        reference_formulation = triple[2]
                    if f"{logical_source_node}|||http://w3id.org/rml/source|||" in triple_str2:
                        triple = triple_str2.split("|||")
                        source_node = triple[2]

                        for triple_str3 in split_graph:
                            if f"{source_node}|||http://w3id.org/rml/path|||" in triple_str3:
                                triple = triple_str3.split("|||")
                                path = triple[2]
                tmp_iterators[path] = {
                    "iterator": iterator,
                    "reference_formulation": reference_formulation,
                }
        iterators.append(tmp_iterators)
    return iterators

####################################################################################################################
#### EXECUTION
####################################################################################################################

def run_mapping(mapping_config):
    if mapping_config.plan == "":
        ############ Generate Plan and Execute ############
        ### STEP 1: Parse & Validate ###
        if mapping_config.validate_shacl:
            shacl_validation_start_time = time.time()
            validate_rml_with_shacl(mapping_config.mapping_source)
            if mapping_config.show_output:
                print("SHACL validation: ", time.time() - shacl_validation_start_time)

        load_rml_start_time = time.time()
        rml_str = load_rml(mapping_config.mapping_source, mapping_config)
        if mapping_config.show_output:
            print("RML loading: ", time.time()-load_rml_start_time)

        ###########################################################
        ## TODO: Move to cpp
        ## Code to handle unique function names

        def get_object(s, p, lines):
            result = []

            for line in lines:
                if line == "":
                    continue

                triple = line.split("|||")

                if triple[0] == s and triple[1] == p:
                    result.append(triple[2])

            return result
        
        def get_subject(p, o, lines):
            result = []

            for line in lines:
                if line == "":
                    continue

                triple = line.split("|||")

                if triple[1] == p and triple[2] == o:
                    result.append(triple[0])

            return result
        
        def set_object(entry, lines):
            s = entry[0]
            p = entry[1]
            new_o = entry[2]
            for i, line in enumerate(lines):
                if line == "":
                    continue

                triple = line.split("|||")

                if triple[0] == s and triple[1] == p:
                    triple[2] = new_o
                    lines[i] = "|||".join(triple)
                    return True  # updated first match

            return False  # nothing found
        

        def get_all_predicates_objects(subject, lines):
            """Return all outgoing (predicate, object) pairs for a subject."""
            result = []
            for line in lines:
                if line == "":
                    continue
                s, p, o = line.split("|||")
                if s == subject:
                    result.append((p, o))
            return result
        
        def new_bnode(lines, prefix="b"):
            """Generate a fresh blank-node id like b12, b13, ..."""
            used = set()
            for line in lines:
                if line == "":
                    continue
                s, p, o = line.split("|||")
                if s.startswith(prefix):
                    used.add(s)
                if o.startswith(prefix):
                    used.add(o)

            max_id = 0
            for node in used:
                suffix = node[len(prefix):]
                if suffix.isdigit():
                    max_id = max(max_id, int(suffix))

            return f"{prefix}{max_id + 1}"
        
        def replace_blank_subjectmap_with_function(lines, constant_value="dt3fav"):
            """
            Find:
                rml:subjectMap [ rml:termType rml:BlankNode ]
            where the subjectMap blank node has nothing else inside it,
            and add the functionExecution structure.
            """
            
            RML = "http://w3id.org/rml/"

            P_SUBJECT_MAP = RML + "subjectMap"
            P_TERM_TYPE = RML + "termType"
            O_BLANK_NODE = RML + "BlankNode"

            P_FUNCTION_EXECUTION = RML + "functionExecution"
            P_FUNCTION = RML + "function"
            P_INPUT = RML + "input"
            P_INPUT_VALUE_MAP = RML + "inputValueMap"
            P_CONSTANT = RML + "constant"

            FN_GENERATE_UNIQUE_IRI = "https://w3id.org/imec/idlab/function#generateUniqueIRI"
           
            changed = False

            # find all subjectMap objects
            subject_maps = []
            for line in lines:
                if line == "":
                    continue
                s, p, o = line.split("|||")
                if p == P_SUBJECT_MAP:
                    subject_maps.append(o)

            for sm in subject_maps:
                outgoing = get_all_predicates_objects(sm, lines)

                # exact match: only one outgoing triple, and it is termType BlankNode
                if len(outgoing) == 1 and outgoing[0] == (P_TERM_TYPE, O_BLANK_NODE):
                    # avoid double insertion
                    if get_object(sm, P_FUNCTION_EXECUTION, lines):
                        continue

                    b_fx = new_bnode(lines)
                    b_input = new_bnode(lines)
                    b_ivm = new_bnode(lines)

                    lines.append(f"{sm}|||{P_FUNCTION_EXECUTION}|||{b_fx}")
                    lines.append(f"{b_fx}|||{P_FUNCTION}|||{FN_GENERATE_UNIQUE_IRI}")
                    lines.append(f"{b_fx}|||{P_INPUT}|||{b_input}")
                    lines.append(f"{b_input}|||{P_INPUT_VALUE_MAP}|||{b_ivm}")
                    lines.append(f"{b_ivm}|||{P_CONSTANT}|||{constant_value}")

                    changed = True

            return changed, lines

        lines = rml_str.split("\n")
        #for line in lines:
            #print(line)

        #############################################################################
        # Replace fixed rml:termType rml:BlankNode with function call.
        changed, lines = replace_blank_subjectmap_with_function(lines, constant_value="dt3fav")
        
        #############################################################################
        # Handle funciton calls
        to_add = []

        function_nodes = get_subject("http://w3id.org/rml/function", "https://w3id.org/imec/idlab/function#generateUniqueIRI", lines)

        ts_ms = str(int(time.time() * 1000)) # Get TS as base for URIs
        cnt = 0 # For identifing unique URIs

        for function_node in function_nodes:
            input_nodes = get_object(function_node, "http://w3id.org/rml/input", lines)
            #print("input_nodes", input_nodes)
            input_node = input_nodes[0]
            
            inputValueMap_nodes = get_object(input_node, "http://w3id.org/rml/inputValueMap", lines)
            #print("inputValueMap_nodes", inputValueMap_nodes)
            inputValueMap_node = inputValueMap_nodes[0]

            uris = get_object(inputValueMap_node, "http://w3id.org/rml/constant", lines)
            #print("uris", uris)
            uri = uris[0]
            
            # Add "/" if not available
            if uri[-1] != "/":
                uri += "/"

            new_object = uri + ts_ms + str(cnt)
            cnt += 1
            to_add.append([inputValueMap_node, "http://w3id.org/rml/constant", new_object])

        # Add new URIs to lines
        for entry in to_add:
            set_object(entry, lines)

        rml_str = "\n".join(lines)

        ###########################################################


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
        base_uris = get_base_uris(normalized_graphs_arr, mapping_config.base_uri)
        
        ### STEP 4: Logical plan generation ###
        convert_to_ra_start_time = time.time()
        ra_expressions, ra_expressions_iterators, ra_expressions_base_uris = convert_to_ra(normalized_graphs_arr, iterators, base_uris, mapping_config)
        ra_str = to_ra_string(ra_expressions)
        if mapping_config.show_output:
            print("Converting to RA: ", time.time()-convert_to_ra_start_time)

        #print(ra_str)

        ### Normalize JSONPath references used in generated plans.
        ra_str = re.sub(r"\$\['([^']+)'\]", r"\1", ra_str)
        ra_str = ra_str.replace("$.", "")
        
        # just return the plan
        if mapping_config.generate_plan:
            print(f"{ra_str}<==>{ra_expressions_iterators}<==>{ra_expressions_base_uris}")
        else:
            triple = run_converter(ra_str, mapping_config.output_file_path, mapping_config.base_uri, mapping_config.continue_on_error, mapping_config.threading_enabled, 
                        mapping_config.materialize_constants, mapping_config.heuristic_ordering, mapping_config.return_triple, mapping_config.data, ra_expressions_iterators, ra_expressions_base_uris)
            
            return triple
    else:
        # Just execute
        import ast
        plan_parts = mapping_config.plan.split("<==>")
        ra_str = plan_parts[0]
        ra_expressions_iterators = plan_parts[1]
        ra_expressions_iterators = ast.literal_eval(ra_expressions_iterators)
        if len(plan_parts) > 2:
            ra_expressions_base_uris = ast.literal_eval(plan_parts[2])
        else:
            ra_expressions_base_uris = []
        triple = run_converter(ra_str, mapping_config.output_file_path, mapping_config.base_uri, mapping_config.continue_on_error, mapping_config.threading_enabled, 
                        mapping_config.materialize_constants, mapping_config.heuristic_ordering, mapping_config.return_triple, mapping_config.data, ra_expressions_iterators, ra_expressions_base_uris)
        return triple

####################################################################################################################
# Function to use as library
def execute(mapping_source = None, plan = None, base_uri = BASE_URI, generate_plan = False, use_threading = True, data = {}, validate_shacl = False):
    config = Configuration()

    if mapping_source:
        config.mapping_source = mapping_source
    elif plan:
        config.plan = plan
    else:
        raise Exception("No plan or mapping provided.")

    config.generate_plan = generate_plan
    config.base_uri = base_uri
    config.threading_enabled = str(use_threading).lower()
    config.data = data
    config.validate_shacl = validate_shacl

    triple = run_mapping(config)

    return triple

####################################################################################################################
# Function executed if used form CLI
def main():
    ### Handle CLI input ###
    config = Configuration()
    
    parser = argparse.ArgumentParser(description="flexrml: An experimental, really fast RML interpreter. Note: stability not guaranteed.")
    parser.add_argument("-m", "--mapping", type=str, required=False, help="The path to the RML mapping file or the content of the mapping file.")
    parser.add_argument("-o", "--output", type=str, required=False, help="The path where the output RDF graph is stored.")
    parser.add_argument("-b", "--base", type=str, required=False, help="The base URI used to generate RDF terms.")
    parser.add_argument("-v", "--version", action='store_true', help="Displays the version of this FlexRML build.")
    parser.add_argument("-gp", "--generate-plan", action='store_true', help="Return only plan.")
    parser.add_argument("-p", "--plan", type=str, required=False, help="Execute mapping form existing plan.")
    parser.add_argument("--continue-on-error", action='store_true', help="Continues on error if the flag is set.")
    parser.add_argument("--no-threading", action='store_false', help="Disables multithreading during execution.")
    parser.add_argument("--no-const-folding", action='store_false', help="Disables constant folding optimization.")
    parser.add_argument("--no-ordering", action='store_false', help="Disables heuristic ordering optimization.")
    parser.add_argument("--validate-shacl", action='store_true', help="Validate the input RML mapping with the official bundled RML-Core SHACL shape before execution.")

    args = parser.parse_args()

    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(0) 

    if args.version:
        print(f"flexrml {config.version} {config.type} - experimental. really fast. stability not guaranteed.")
        sys.exit(0)

    if args.mapping:
        config.mapping_source = args.mapping
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
        config.heuristic_ordering = str(args.no_ordering).lower()

    if args.generate_plan == False:
        config.generate_plan = False

    if args.validate_shacl:
        if config.plan:
            print("Error: --validate-shacl can only be used with --mapping, not with --plan.")
            sys.exit(1)
        config.validate_shacl = True

    config.return_triple = False # Do not return triple, just display

    ### Execute ###
    run_mapping(config)

        
if __name__ == "__main__":
    #start_time = time.time()
    main()
    #print("Total time:", time.time() - start_time)
