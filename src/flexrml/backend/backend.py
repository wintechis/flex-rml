import ctypes
import os
import sys
import re
from collections import defaultdict
import time
import json
from jsonpath_ng import parse
#import polars as pl
import io
import csv
from pathlib import Path
import xml.etree.ElementTree as ET

def package_root() -> Path:
    here = Path(__file__).resolve().parent
    if here.name == "backend":
        return here
    return here / "flexrml" / "backend"

def resource_path(*parts: str) -> str:
    return str(package_root().joinpath(*parts))

class Configuration:
    def __init__(self):
        self.output_file_path = ""
        self.base_uri = "http://example.com/base/"
        self.continue_on_error = "false"
        self.threading_enabled = "true"
        self.materialize_constants = "true"
        self.heuristic_ordering = "true"
        self.keep_in_memory = "false"
        self.return_triple = False
        self.data = {}
        self.base_uris = []

        self.show_output = False
        self.bn_number = 58932
        self.iterators = []
        self.lib_ra_partitioner = self.load_ra_partitioner()
        self.lib_plan_executor = self.load_plan_executor()
        self.lib_threaded_plan_executor = self.load_threaded_plan_executor()

    def _load_cdll(self, relative_path: str) -> ctypes.CDLL:
        lib_path = resource_path(*relative_path.split("/"))

        try:
            return ctypes.CDLL(lib_path)
        except OSError as e:
            print(f"Error loading '{lib_path}': {e}")
            sys.exit(1)

    def load_ra_partitioner(self):
        lib = self._load_cdll("librapartitioner.so")
        lib.ra_partitioner.argtypes = [ctypes.c_char_p]
        lib.ra_partitioner.restype = ctypes.c_char_p
        return lib
    
    def load_plan_executor(self):
        lib = self._load_cdll("libexecutor.so")
        lib.execute_physical_plans.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
        lib.execute_physical_plans.restype = ctypes.c_char_p
        return lib       

    def load_threaded_plan_executor(self):
        lib = self._load_cdll("libthreadexecutor.so")
        lib.simple_threaded_mapping.argtypes = [ctypes.c_char_p]
        lib.simple_threaded_mapping.restype = ctypes.c_int
        return lib

####################################################################################################################

def pretty_print_nt(rdf_str):
    rdf_arr = rdf_str.split("\n")
    for triple_str in rdf_arr:
        triple = triple_str.split("|||")
        if len(triple) != 3:
            continue

        
        subj = "" 
        pred = "<" + triple[1]+ ">"
        obj = ""

        if triple[0][0:4] == "http":
            subj = "<" + triple[0] + ">"
        elif triple[0][0] == "b" and triple[0][1:].isdigit():
            subj = "_:" + triple[0]
        else:
            print("Error!", triple[0])
        
        
        if pred == "<http://www.w3.org/ns/r2rml#template>":
            obj  = "\"" + triple[2] + "\"" 
        elif triple[2][0:4] == "http":
            obj = "<" + triple[2] + ">"
        elif triple[2][0] == "b" and triple[2][1:].isdigit():
            obj = "_:" + triple[2]
        else:
            obj = "\"" + triple[2] + "\"" 
        
        print(f"{subj} {pred} {obj} .")

###############################################################################

def _split_projection_segment(segment: str) -> tuple[str, str]:
    try:
        arguments, source = segment.rsplit("](", 1)
    except ValueError:
        print("Could not parse projection segment. Got:")
        print(segment)
        sys.exit(1)

    return arguments.strip(), source.strip().rstrip(")").strip()

def parse_ra(ra_expressions):
    new_ra_expressions = []

    for ra_expression in ra_expressions:
        #ra_expression = remove_whitespace_except_first_arg(ra_expression)
        pi_count = ra_expression.count("pi[")

        # Simple mapping has 2 projections (pi)
        if pi_count == 2:
            # Handle projection
            projection_arguments, source = _split_projection_segment(ra_expression.split("pi[")[-1])
            arguments = projection_arguments.replace(",","===")
            projection_node = {"type": "projection", "in_relation": source, "arguments": arguments}
            # Handle creation
            create_count = ra_expression.split("pi[")[1].count("create(")

            if create_count == 3:
                s_content = ra_expression.split("pi[")[1].split("create(")[1].split(")")[0].replace(")","").replace(",", "===")
                p_content = ra_expression.split("pi[")[1].split("create(")[2].split(")")[0].replace(")","").replace(",", "===")
                o_content = ra_expression.split("pi[")[1].split("create(")[3].split(")")[0].replace(")","").replace(",", "===")

                creation = {"type": "creation", "s_content": s_content, "p_content": p_content, "o_content": o_content}
                new_ra_expressions.append([projection_node, creation])

            elif create_count == 4:
                s_content = ra_expression.split("pi[")[1].split("create(")[1].split(")")[0].replace(")","").replace(",", "===")
                p_content = ra_expression.split("pi[")[1].split("create(")[2].split(")")[0].replace(")","").replace(",", "===")
                o_content = ra_expression.split("pi[")[1].split("create(")[3].split(")")[0].replace(")","").replace(",", "===")
                g_content = ra_expression.split("pi[")[1].split("create(")[4].split(")")[0].replace(")","").replace(",", "===")

                creation = {"type": "creation", "s_content": s_content, "p_content": p_content, "o_content": o_content, "g_content": g_content}
                new_ra_expressions.append([projection_node, creation])
            else:
                print("Could not parse relational algebra expression.")
                sys.exit(1)

        elif pi_count == 3:
            projection_arguments1, source1 = _split_projection_segment(ra_expression.split("pi[")[2].split("bowtie")[0])
            arguments1 = projection_arguments1.replace(",", "===").strip()
            projection_node1 = {"type": "projection", "in_relation": source1, "arguments": arguments1, "name": "parent"}

            projection_arguments2, source2 = _split_projection_segment(ra_expression.split("pi[")[-1])
            arguments2 = projection_arguments2.replace(",", "===").strip()
            projection_node2 = {"type": "projection", "in_relation": source2, "arguments": arguments2, "name": "child"}

            #new_ra_expressions.append([{"type": "projection", "in_relation": in_relation_left, "arguments": right_args + "==="+ left_args,},
                    #{"type": "creation", "s_content": s_content, "p_content": p_content, "o_content": o_content}])

            if len(ra_expression.split("pi[")[2].split("bowtie")[1].split("[")) == 1:
                # Handle previous natural join
                if source1 != source2:
                    print("Currently not supported.")
                    sys.exit()
                join_node = ""
            else:
                join_arguments = ra_expression.split("pi[")[2].split("bowtie")[1].split("[")[1].split("]")[0].replace("=", "===").strip()

                # Rename as parent and child
                join_arg_left = join_arguments.split("===")[0].replace(source1, "parent")
                join_arg_right = join_arguments.split("===")[1].replace(source2, "child")
                join_arguments = f"{join_arg_left}==={join_arg_right}"

                join_node = {"type": "equi-join", "arguments": join_arguments}

            create_count = ra_expression.split("pi[")[1].count("create(")

            if create_count == 3:
                s_content = ra_expression.split("pi[")[1].split("create")[1].split(")")[0].split("(")[1].replace(",", "===").strip()
                p_content = ra_expression.split("pi[")[1].split("create(")[2].split(")")[0].replace(",", "===").strip()
                o_content = ra_expression.split("pi[")[1].split("create(")[3].split(")")[0].split(")")[0].replace(",", "===").strip() 

                # Rename as parent and child
                s_content = s_content.replace(source1, "parent")
                p_content = p_content.replace(source1, "parent")           
                o_content = o_content.replace(source2, "child")

                creation = {"type": "creation", "s_content": s_content, "p_content": p_content, "o_content": o_content}

                if join_node == "":
                    new_ra_expressions.append([{"type": "projection", "in_relation": source1, "arguments": arguments1 + "==="+ arguments2,},
                    {"type": "creation", "s_content": s_content, "p_content": p_content, "o_content": o_content}])
                else:
                    new_ra_expressions.append([projection_node1, projection_node2, join_node, creation])

            elif create_count == 4:
                s_content = ra_expression.split("pi[")[1].split("create")[1].split(")")[0].split("(")[1].replace(",", "===").strip()
                p_content = ra_expression.split("pi[")[1].split("create(")[2].split(")")[0].replace(",", "===").strip()
                o_content = ra_expression.split("pi[")[1].split("create(")[3].split(")")[0].replace(")","").replace(",", "===").strip()
                g_content = ra_expression.split("pi[")[1].split("create(")[4].split(")")[0].replace(")","").replace(",", "===").strip()
                
                # Rename as parent and child
                s_content = s_content.replace(source1, "parent")
                p_content = p_content.replace(source1, "parent")           
                o_content = o_content.replace(source2, "child")

                if join_node == "":
                    new_ra_expressions.append([{"type": "projection", "in_relation": source1, "arguments": arguments1 + "===" + arguments2},
                    {"type": "creation", "s_content": s_content, "p_content": p_content, "o_content": o_content, "g_content": g_content}])
                else:
                    creation = {"type": "creation", "s_content": s_content, "p_content": p_content, "o_content": o_content, "g_content": g_content}
                    new_ra_expressions.append([projection_node1, projection_node2, join_node, creation])

            else:
                print("Could not parse relational algebra expression.")
                sys.exit(1)

        else:
            print("Detected unsupported relational algebra expression. Got:")
            print(ra_expression)
            sys.exit(1)

    return new_ra_expressions

##########################################################################################
def create_physical_plans(ra_expressions, config):
    physical_plans = []
    for i, ra_expression in enumerate(ra_expressions):
        plan_base_uri = config.base_uris[i] if i < len(config.base_uris) else config.base_uri
        # Handle simple plan
        if len(ra_expression) == 2:
            # Without graph
            if len(ra_expression[1]) == 4:
                plan = [["seq_scan", ra_expression[0]["in_relation"], ra_expression[0]["arguments"]],
                        ["format", ra_expression[1]["s_content"], ra_expression[1]["p_content"], ra_expression[1]["o_content"]],
                        [config.output_file_path], [plan_base_uri], [config.continue_on_error]]
            else:
                plan = [["seq_scan", ra_expression[0]["in_relation"], ra_expression[0]["arguments"]],
                        ["format", ra_expression[1]["s_content"], ra_expression[1]["p_content"], ra_expression[1]["o_content"], ra_expression[1]["g_content"]],
                        [config.output_file_path], [plan_base_uri], [config.continue_on_error]]
            physical_plans.append(plan)
        elif len(ra_expression) == 4:
            if len(ra_expression[3]) == 4:
                plan = [["seq_scan", ra_expression[0]["in_relation"], ra_expression[0]["arguments"], ra_expression[0]["name"]],
                        ["seq_scan", ra_expression[1]["in_relation"], ra_expression[1]["arguments"], ra_expression[1]["name"]],
                        ["hash_join", ra_expression[2]["arguments"]],
                        ["format", ra_expression[3]["s_content"], ra_expression[3]["p_content"], ra_expression[3]["o_content"]],
                        [config.output_file_path], [plan_base_uri], [config.continue_on_error]]
            else:
                plan = [["seq_scan", ra_expression[0]["in_relation"], ra_expression[0]["arguments"], ra_expression[0]["name"]],
                        ["seq_scan", ra_expression[1]["in_relation"], ra_expression[1]["arguments"], ra_expression[1]["name"]],
                        ["hash_join", ra_expression[2]["arguments"]],
                        ["format", ra_expression[3]["s_content"], ra_expression[3]["p_content"], ra_expression[3]["o_content"], ra_expression[3]["g_content"]],
                        [config.output_file_path], [plan_base_uri], [config.continue_on_error]]
            physical_plans.append(plan)
        else:
            print("Unsupported length of ra expression!. Got: ", len(ra_expression))
            sys.exit(1)
    
    return physical_plans
##########################################################################################
# Remove self joins
def remove_self_join(physical_plans):
    new_physical_plans = []
    
    for plan in physical_plans:
        # must be join
        if len(plan) != 7:
            new_physical_plans.append(plan)
            continue

        # check if both have the same source
        if plan[0][1] != plan[1][1]:
            new_physical_plans.append(plan)
            continue

        # Extract projected arguments
        args_1 = plan[0][2].split("===")
        args_2 = plan[1][2].split("===")

        combined_args = args_1 + args_2
        print(combined_args)
        print("====")

        new_scan = ['seq_scan', plan[0][1], ("===").join(combined_args), plan[0][1]]

        # Handle format 
        subject = plan[3][1]
        new_subject = subject.replace(f"{plan[0][1]}_", "")
        prediacte = plan[3][2]
        new_prediacte = prediacte.replace(f"{plan[0][1]}_", "")
        object = plan[3][3]
        new_object = object.replace(f"{plan[0][1]}_", "")

        new_format = ["format", new_subject, new_prediacte, new_object]

        if len(plan[3]) == 5:
            graph = plan[3][4]
            new_graph = graph.replace(f"{plan[0][1]}_", "")
            new_format.append(new_graph)


        new_plan = [new_scan, new_format, plan[4], plan[5], plan[6]]
        new_physical_plans.append(new_plan)

    return new_physical_plans


##########################################################################################
def constant_folding(ra_expressions):
    for ra_expression in ra_expressions:
        # Simple Mapping
        if len(ra_expression) == 2:
            create_funciton_elements = ra_expression[1]
        elif len(ra_expression) == 4:
            create_funciton_elements = ra_expression[3]
        else:
            print("Constant folding not supported.!")
            sys.exit(1)

        s_content = create_funciton_elements["s_content"].split("===")
        p_content = create_funciton_elements["p_content"].split("===")
        o_content = create_funciton_elements["o_content"].split("===")
        
        if len(create_funciton_elements) > 4:
            g_content = create_funciton_elements["g_content"].split("===")

            content = [s_content, p_content, o_content, g_content]
        
        else:
            content = [s_content, p_content, o_content]

        for i in range(len(content)):
            element = content[i]
            
            # Handle templates that are actually constant
            if element[1] == "template":
                if "{" not in element[0]:
                     element[1] = "constant"
            
            if element[1] != "constant":
                continue

            if element[2] == "iri":
                constant_value = f"<{element[0]}>"
            elif element[2] == "blanknode":
                constant_value = f"_:{element[0]}"
            elif element[2] == "literal":
                inferred_datatype = element[4]
                if element[3] == "None" and element[4] == "None":
                    if element[0] == "true" or element[0] == "false":
                        inferred_datatype = "http://www.w3.org/2001/XMLSchema#boolean"
                    elif re.match(r"^-?(0|[1-9][0-9]*)$", element[0]):
                        inferred_datatype = "http://www.w3.org/2001/XMLSchema#integer"
                    elif re.match(r"^-?(0|[1-9][0-9]*)\.[0-9]+$", element[0]):
                        inferred_datatype = "http://www.w3.org/2001/XMLSchema#decimal"

                constant_value = f"\"{element[0]}\""
                if element[3] != "None":
                    constant_value += f"@{element[3]}"
                elif inferred_datatype != "None":
                    constant_value += f"^^<{inferred_datatype}>"

            if i == 0:
                create_funciton_elements["s_content"] = f"{constant_value}===preformatted===xxx"
            elif i == 1:
                create_funciton_elements["p_content"] = f"{constant_value}===preformatted===xxx"
            elif i == 2:
                create_funciton_elements["o_content"] = f"{constant_value}===preformatted===xxx"
            elif i == 3:
                create_funciton_elements["g_content"] = f"{constant_value}===preformatted===xxx"

    return ra_expressions

##########################################################################################
def standard_threading(plan_partitions, config, start_time, in_memory_data):
    plans = ""
    for partition in plan_partitions:
        for plan in partition:
            plans += phys_plan_to_str(plan).strip() + "PxPwPePrP"
        plans += "TTTtttTTTtttTTT"
    plans = plans.strip().encode()
    lib = config.lib_plan_executor
    output = lib.execute_physical_plans(plans, config.threading_enabled.encode(), config.continue_on_error.encode(), config.output_file_path.encode(), config.keep_in_memory.encode(), in_memory_data.encode())
    output = output.decode()
    generated_triple = output.split("|||")[0]
    triple_string = output.split("|||")[1]
    
    if config.show_output:
        print(f"Execution finished.\nGenerated: {generated_triple} triple in {time.time()-start_time:.3f} seconds.")

    if config.keep_in_memory:
        if config.return_triple:
            return triple_string.strip()
        else:
            print(triple_string.strip())


def alternative_threading(plan_partitions, config, start_time):
    with open(config.output_file_path, 'w') as file:
        pass
    generated_triple = 0
    for partition in plan_partitions:
        plans = ""
        for plan in partition:
            plans += phys_plan_to_str(plan).strip() + "PxPwPePrP"
        if "hash_join" in plans:
            standard_threading(plan_partitions, config, start_time)
        else:
            if config.threading_enabled == "false":
                standard_threading(plan_partitions, config, start_time)
            else:
                lib = config.lib_threaded_plan_executor
                generated_triple += lib.simple_threaded_mapping(plans.encode())
                print(f"Execution threading finished.\nGenerated: {generated_triple} triple in {time.time()-start_time:.3f} seconds.")

##########################################################################################

def phys_plan_to_str(plan):
    plan_str = ""
    for element in plan:
        plan_str += ("|||").join(element) + "\n"
    plan_str = plan_str
    return plan_str

def flatten_dict(d, parent_key="", sep="."):
    items = []
    for k, v in d.items():
        new_key = f"{parent_key}{sep}{k}" if parent_key else k
        if isinstance(v, dict):
            items.extend(flatten_dict(v, new_key, sep=sep).items())
        else:
            items.append((new_key, v))
    return dict(items)

def expand_json_row(value, parent_key="", sep="."):
    if isinstance(value, dict):
        rows = [{}]
        for key, nested_value in value.items():
            nested_key = f"{parent_key}{sep}{key}" if parent_key else key
            nested_rows = expand_json_row(nested_value, nested_key, sep=sep)
            rows = [{**base_row, **nested_row} for base_row in rows for nested_row in nested_rows]
        return rows

    if isinstance(value, list):
        if not value:
            return []

        list_key = f"{parent_key}[*]" if parent_key else "[*]"
        rows = []
        for item in value:
            if isinstance(item, (dict, list)):
                rows.extend(expand_json_row(item, list_key, sep=sep))
            else:
                rows.append({list_key: item})
        return rows

    return [{parent_key: value}]

def preprocess_json(in_relation, json_path_expr_dict, data=None):
    if data is None:
        with open(in_relation, 'r', encoding='utf-8') as f:
            data = f.read()

    data = json.loads(data)

    json_path_expr = json_path_expr_dict[in_relation]

    expr = parse(json_path_expr)
    matches = expr.find(data)
    rows = [m.value for m in matches]

    if not rows:
        return ""

    if isinstance(rows[0], dict):
        flat_rows = []
        for row in rows:
            flat_rows.extend(expand_json_row(row))

        if not flat_rows:
            return ""

        fieldnames = sorted({k for r in flat_rows for k in r.keys()})
        buf = io.StringIO()
        writer = csv.DictWriter(buf, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(flat_rows)
        return buf.getvalue()

    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(["value"])
    for r in rows:
        writer.writerow([r])
    return buf.getvalue()


def _split_xpath(xpath_expr):
    return [part for part in xpath_expr.strip().split("/") if part]


def _element_text(element):
    text = "".join(element.itertext()).strip()
    return text


def _select_xpath_nodes(root, xpath_expr):
    parts = _split_xpath(xpath_expr)
    if not parts:
        return []

    nodes = [root]
    if parts and parts[0] == root.tag:
        parts = parts[1:]

    for part in parts:
        next_nodes = []
        for node in nodes:
            next_nodes.extend(list(node.findall(part)))
        nodes = next_nodes
        if not nodes:
            break

    return nodes


def _flatten_xml_element(element, parent_key=""):
    row = {}

    text = _element_text(element)
    if text and not list(element):
        row[parent_key or element.tag] = text

    for attr_name, attr_value in element.attrib.items():
        attr_key = f"{parent_key}.@{attr_name}" if parent_key else f"@{attr_name}"
        row[attr_key] = attr_value

    children = list(element)
    if not children:
        return row

    grouped_children = defaultdict(list)
    for child in children:
        grouped_children[child.tag].append(child)

    for child_tag, child_elements in grouped_children.items():
        child_key = f"{parent_key}.{child_tag}" if parent_key else child_tag
        if len(child_elements) == 1:
            row.update(_flatten_xml_element(child_elements[0], child_key))
            continue

        values = []
        for child_element in child_elements:
            child_text = _element_text(child_element)
            if child_text and not list(child_element) and not child_element.attrib:
                values.append(child_text)
            else:
                nested = _flatten_xml_element(child_element, child_key)
                for nested_key, nested_value in nested.items():
                    row.setdefault(nested_key, nested_value)
        if values:
            row[child_key] = "|".join(values)

    return row


def preprocess_xml(in_relation, iterator_config, data=None):
    if data is None:
        with open(in_relation, "r", encoding="utf-8") as f:
            data = f.read()

    root = ET.fromstring(data)
    xpath_expr = iterator_config["iterator"]
    rows = [_flatten_xml_element(node) for node in _select_xpath_nodes(root, xpath_expr)]
    rows = [row for row in rows if row]

    if not rows:
        return ""

    fieldnames = sorted({key for row in rows for key in row.keys()})
    buf = io.StringIO()
    writer = csv.DictWriter(buf, fieldnames=fieldnames, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(rows)
    return buf.getvalue()


def preprocess_input(in_relation, iterator_config, data=None):
    if not isinstance(iterator_config, dict):
        return preprocess_json(in_relation, {in_relation: iterator_config}, data)

    reference_formulation = iterator_config.get("reference_formulation")

    if reference_formulation == "http://w3id.org/rml/JSONPath":
        return preprocess_json(in_relation, {in_relation: iterator_config["iterator"]}, data)
    if reference_formulation == "http://w3id.org/rml/XPath":
        return preprocess_xml(in_relation, iterator_config, data)

    print(f"Unsupported reference formulation for source '{in_relation}': {reference_formulation}")
    sys.exit(1)


def start_conversion(ra_expressions, config = None):
    """
    Main function for running the mapping
    Input is ra_expression list and optional a config otherwise the default values are used
    """
    start_time = time.time()
    loaded_relations = set()
    ra_expressions = [item for item in ra_expressions if item]

    # Setup config
    if config == None:
        config = Configuration()

    
    ### Parse RA expressions
    ra_expressions = parse_ra(ra_expressions)
    
    in_memory_data = "" # Stores final output string

    ### Preprocess input data
    for i in range(len(ra_expressions)):
        ra_expr = ra_expressions[i]
        iterators = config.iterators[i]

        # Build in_relations array
        if len(ra_expr) == 2:
            in_relations = [ra_expr[0]["in_relation"]]
        elif len(ra_expr) == 4:
            in_relations = [ra_expr[0]["in_relation"], ra_expr[1]["in_relation"]]
        else:
            print("Unsupported size! Expected 2 or 4. Got ", len(ra_expr))
            continue
        
        # Handle data
        for in_relation in in_relations:

            if in_relation in loaded_relations:
                continue

            # Get in memory data
            in_memory_data_value = config.data.get(in_relation, None)
            # Get iterator
            iterator = iterators.get(in_relation)
            
            # Case: Already CSV
            if in_memory_data_value != None and iterator == None:
                # Add to string, assume that it is already CSV
                in_memory_data += f"|||===|||{in_relation}===|||==={in_memory_data_value}"
                loaded_relations.add(in_relation)
                continue
            
            # Case: No preprocessing needed.
            if iterator == None:
                continue
            
            # Case: Transformation needed.
            csv_str = preprocess_input(in_relation, iterator, in_memory_data_value)

            loaded_relations.add(in_relation)
            in_memory_data += f"|||===|||{in_relation}===|||==={csv_str}"


    #################################################
    # Simple RA Expression
    # pi[PROJ_ATTR1,PROJ_ATTR2](source_rel)
    # pi[create(), create(), create(), create()]()

    # π[create(), create(), create(), create()](π[PROJ_ATTR1,PROJ_ATTR2](source_rel))
    # pi[create(), create(), create(), create()](pi[PROJ_ATTR1,PROJ_ATTR2](source_rel))

    #####
    # Complex RA Expression
    # pi[PROJ_ATTR1,PROJ_ATTR2](source_rel1)
    # pi[PROJ_ATTR1,PROJ_ATTR2](source_rel2)
    # R join[CONDITION1=CONDITION2] S
    # pi[create(), create(), create(), create()]()

    # π[create(), create(), create(), create()]( (π[PROJ_ATTR1,PROJ_ATTR2](source_rel1)) ⋈ [A=B] (π[PROJ_ATTR1,PROJ_ATTR2](source_rel2)) )
    # pi[create(), create(), create(), create()]( (pi[PROJ_ATTR1,PROJ_ATTR2](source_rel1)) bowtie [A=B] (pi[PROJ_ATTR1,PROJ_ATTR2](source_rel2)) )
    # 
    
    # pi[create() -> S,create() -> P,create() -> O]( (pi["arrival_time","stop_id","trip_id"](STOP_TIMES.csv) ) bowtie [STOP_TIMES.csv.trip_id=TRIPS.csv.trip_id] (pi["trip_id"](TRIPS.csv)))

    #################################################

    ############################
    ### Logical Optimization ###
    ############################

    # Partitioning
    tmp = ""
    for ra_expression in ra_expressions:
        if len(ra_expression) == 2:
            if len(ra_expression[1]) == 4:
                tmp += ra_expression[1]["s_content"] + "|||" + ra_expression[1]["p_content"] + "|||" + ra_expression[1]["o_content"] + "\n"
            else:
                tmp += ra_expression[1]["s_content"] + "|||" + ra_expression[1]["p_content"] + "|||" + \
                    ra_expression[1]["o_content"] + ra_expression[1]["g_content"] + "\n"
        elif len(ra_expression) == 4:
            if len(ra_expression[3]) == 4:
                tmp += ra_expression[3]["s_content"] + "|||" + ra_expression[3]["p_content"] + "|||" + ra_expression[3]["o_content"] + "\n"
            else:
                tmp += ra_expression[3]["s_content"] + "|||" + ra_expression[3]["p_content"] + "|||" + \
                    ra_expression[3]["o_content"] + ra_expression[3]["g_content"] + "\n"
    
    tmp = tmp.strip().encode()
    lib = config.lib_ra_partitioner
    result = lib.ra_partitioner(tmp)
    result_str = result.decode().strip()
    result_arr = result_str.split("\n")
    
    data = []
    for res in result_arr:
        data.append(res.split("|||"))
        
    sorted_data = sorted(data, key=lambda x: int(x[0]))
    partitioning_result = [item[1] for item in sorted_data]

    # Constant Folding: recognizing and evaluating constant expressions at compile time
    if config.materialize_constants == "true":
        ra_expressions = constant_folding(ra_expressions)

    ################################
    ### Physical Plan Generation ###
    ################################

    physical_plans = create_physical_plans(ra_expressions, config)

    ##################################
    ### Physical plan optimization ###
    ##################################

    #physical_plans = remove_self_join(physical_plans)

    # Partition physical plans
    grouped_data = defaultdict(list)
    for id_, element in zip(partitioning_result, physical_plans):
        grouped_data[id_].append(element)

    plan_partitions = dict(grouped_data).values()

    #####################################################

    # Order plans
    if config.heuristic_ordering == "true" and in_memory_data == "":
        ordered_plans = {}
        for plan_partition in plan_partitions:
            total_file_size = 0

            for plan in plan_partition:
                # Simple mapping
                if len(plan) == 5:
                    path = plan[0][1]
                    if os.path.isfile(path):
                        total_file_size += os.path.getsize(path)
                elif len(plan) == 7:  
                    path1 = plan[0][1]
                    if os.path.isfile(path1):
                        total_file_size += os.path.getsize(path1)
                    path2 = plan[1][1]
                    if os.path.isfile(path2):
                        total_file_size += os.path.getsize(path2)
                else:
                    print("Error ordering plans")
                    sys.exit()

            # Store multiple entries under the same file size
            if total_file_size not in ordered_plans:
                ordered_plans[total_file_size] = []
            ordered_plans[total_file_size].append(plan_partition)

        # Sort and flatten the list
        sorted_dict_desc = dict(sorted(ordered_plans.items(), reverse=True))

        plan_partitions = [partition for partitions in sorted_dict_desc.values() for partition in partitions]

    #####################################################
    #################
    ### Execution ###
    #################  
    try:
        if len(plan_partitions) == 1 and config.keep_in_memory == False and in_memory_data == "":
            alternative_threading(plan_partitions, config, start_time)       
        else:
            triple = standard_threading(plan_partitions, config, start_time, in_memory_data)
            return triple
    except:
        pass # Errors handled inside cpp.

    

##########################################################################################

# Convert data from a local file called "plan"
def konverter_from_file():
    config = Configuration()

    config.threading_enabled = "false"

    ### Load RA file
    with open('plan', 'r') as file:
        ra_str = file.read()
    ra_arr = ra_str.split("\n")

    start_conversion(ra_arr, config)

##########################################################################################

def run_converter(ra_expressions: str, output_file_path: str, base_uri: str, continue_on_error: str, threading_enabled: str, 
                  materialize_constants: str, heuristic_ordering: str, return_triple: bool, data= {}, iterators = [], base_uris = []) -> None:
    """
    Function imported in other projects using the konverter backend.
    Input: The ra_expression as string.
    Output: None
    """
    # Setup config
    config = Configuration()
    config.base_uri = base_uri
    config.continue_on_error = continue_on_error
    config.threading_enabled = threading_enabled
    config.materialize_constants = materialize_constants
    config.heuristic_ordering = heuristic_ordering
    config.output_file_path = output_file_path
    config.iterators = iterators
    config.base_uris = base_uris
    config.return_triple = return_triple

    # In compiled version this is somehow needed.
    # Todo: DEBUG
    if data == None:
        data = {}
    config.data = data

    # If no path is provided just print results at the end
    if config.output_file_path == "":
        config.keep_in_memory = "true"

    # Convert string to list
    ra_arr = ra_expressions.split("\n")

    triple = start_conversion(ra_arr, config)

    return triple

if __name__ == "__main__":
    konverter_from_file()
