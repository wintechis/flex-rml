#!/bin/bash

check_if_exists() {
    local filepath=$1
    if [ ! -f "$filepath" ]; then
        echo "Build failed: $filepath not found."
        exit 1
    else 
        echo "Success."
    fi
}



echo "All files required files are present."
echo ""

echo "Cleaning up old shared object files..."
rm ./frontend/libnormalizer.so ./frontend/libraconverter.so ./frontend/librdfparser.so ./backend/libexecutor.so ./frontend/libfunctionexecutor.so ./backend/librapartitioner.so ./backend/libthreadexecutor.so 
echo ""

###########################

echo "Building rml parser ..."
g++ -std=c++20 -shared -fPIC -Ifrontend/rdf_parser/serd_lib -o ./frontend/librdfparser.so ./frontend/rdf_parser/rdf_parser_lib.cpp ./frontend/rdf_parser/rdf_parser.cpp ./frontend/rdf_parser/serd_lib/*.c -O3
check_if_exists ./frontend/librdfparser.so
echo ""

echo "Building rml normalizer ..."
g++ -std=c++20 -shared -fPIC -o ./frontend/libnormalizer.so ./frontend/rml_normalizer/rml_core_normalizer.cpp -O3
check_if_exists ./frontend/libnormalizer.so
echo ""

echo "Building relational algebra converter ..."
g++ -std=c++20 -shared -fPIC -o ./frontend/libraconverter.so ./frontend/ra_converter/ra_converter_rml_core.cpp -O3
check_if_exists ./frontend/libraconverter.so
echo ""

echo "Building relational algebra converter ..."
g++ -std=c++20 -shared -fPIC -o ./frontend/libfunctionexecutor.so ./frontend/functions/rml_functions.cpp -O3
check_if_exists ./frontend/libfunctionexecutor.so
echo ""

###########################

echo "Building relational algebra partitioner ..."
g++ -std=c++20 -shared -fPIC -o ./backend/librapartitioner.so ./backend/optimizations/ra_expression_partitioner.cpp -O3
check_if_exists ./backend/librapartitioner.so
echo ""

echo "Building executor ..."
g++ -std=c++20 -shared -fPIC -o ./backend/libexecutor.so ./backend/executor/executor.cpp ./backend/executor/xxhash.c ./backend/executor/simple_executor.cpp ./backend/executor/utils.cpp ./backend/executor/complex_executor.cpp -I./backend/executor/ -O3
check_if_exists ./backend/libexecutor.so
echo ""

echo "Building threaded executor ..."
g++ -std=c++20 -shared -fPIC -o ./backend/libthreadexecutor.so ./backend/executor/simple_executor_threaded.cpp ./backend/executor/xxhash.c ./backend/executor/utils.cpp -O3
check_if_exists ./backend/libthreadexecutor.so
echo ""