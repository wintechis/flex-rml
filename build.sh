#!/bin/bash

download_if_not_exists() {
    local filepath=$1
    local url=$2

    if [ ! -f "$filepath" ]; then
        echo "Downloading $filepath..."
        wget -O "$filepath" "$url"
    fi
}

check_if_exists() {
    local filepath=$1
    if [ ! -f "$filepath" ]; then
        echo "Build failed: $filepath not found."
        exit 1
    else 
        echo "Success."
    fi
}

echo "Preparing files ..."
# Download RDF parser code (serd)
download_if_not_exists ./frontend/rdf_parser/serd_lib/serd/serd.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/include/serd/serd.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/attributes.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/attributes.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/base64.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/base64.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/base64.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/base64.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/byte_sink.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/byte_sink.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/byte_source.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/byte_source.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/byte_source.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/byte_source.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/env.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/env.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/n3.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/n3.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/node.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/node.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/node.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/node.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/reader.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/reader.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/reader.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/reader.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/serd_config.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/serd_config.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/serd_internal.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/serd_internal.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/stack.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/stack.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/string.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/string.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/string_utils.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/string_utils.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/system.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/system.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/system.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/system.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/try.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/try.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/uri.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/uri.c
download_if_not_exists ./frontend/rdf_parser/serd_lib/uri_utils.h https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/uri_utils.h
download_if_not_exists ./frontend/rdf_parser/serd_lib/writer.c https://raw.githubusercontent.com/drobilla/serd/abfdac2085cba93bae76d3352f2ff7e40fdf1612/src/writer.c

# Download Hash Function
download_if_not_exists ./backend/executor/xxhash.h https://raw.githubusercontent.com/Cyan4973/xxHash/refs/heads/dev/xxhash.h
download_if_not_exists ./backend/executor/xxhash.c https://raw.githubusercontent.com/Cyan4973/xxHash/refs/heads/dev/xxhash.c

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