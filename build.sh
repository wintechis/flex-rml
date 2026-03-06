#!/bin/bash
set -e

check_if_exists() {
    local filepath=$1
    if [ ! -f "$filepath" ]; then
        echo "Build failed: $filepath not found."
        exit 1
    else
        echo "Success."
    fi
}

PKG=./src/flexrml

echo "Cleaning old shared libraries..."
rm -f \
  $PKG/frontend/libnormalizer.so \
  $PKG/frontend/libraconverter.so \
  $PKG/frontend/librdfparser.so \
  $PKG/frontend/libfunctionexecutor.so \
  $PKG/backend/libexecutor.so \
  $PKG/backend/librapartitioner.so \
  $PKG/backend/libthreadexecutor.so

echo "Building rml parser ..."
g++ -std=c++20 -shared -fPIC \
  -I$PKG/frontend/rdf_parser/serd_lib \
  -o $PKG/frontend/librdfparser.so \
  $PKG/frontend/rdf_parser/rdf_parser_lib.cpp \
  $PKG/frontend/rdf_parser/rdf_parser.cpp \
  $PKG/frontend/rdf_parser/serd_lib/*.c \
  -O3
check_if_exists $PKG/frontend/librdfparser.so
echo ""

echo "Building rml normalizer ..."
g++ -std=c++20 -shared -fPIC \
  -o $PKG/frontend/libnormalizer.so \
  $PKG/frontend/rml_normalizer/rml_core_normalizer.cpp \
  -O3
check_if_exists $PKG/frontend/libnormalizer.so
echo ""

echo "Building relational algebra converter ..."
g++ -std=c++20 -shared -fPIC \
  -o $PKG/frontend/libraconverter.so \
  $PKG/frontend/ra_converter/ra_converter_rml_core.cpp \
  -O3
check_if_exists $PKG/frontend/libraconverter.so
echo ""

echo "Building function executor ..."
g++ -std=c++20 -shared -fPIC \
  -o $PKG/frontend/libfunctionexecutor.so \
  $PKG/frontend/functions/rml_functions.cpp \
  -O3
check_if_exists $PKG/frontend/libfunctionexecutor.so
echo ""

echo "Building relational algebra partitioner ..."
g++ -std=c++20 -shared -fPIC \
  -o $PKG/backend/librapartitioner.so \
  $PKG/backend/optimizations/ra_expression_partitioner.cpp \
  -O3
check_if_exists $PKG/backend/librapartitioner.so
echo ""

echo "Building executor ..."
g++ -std=c++20 -shared -fPIC \
  -o $PKG/backend/libexecutor.so \
  $PKG/backend/executor/executor.cpp \
  $PKG/backend/executor/xxhash.c \
  $PKG/backend/executor/simple_executor.cpp \
  $PKG/backend/executor/utils.cpp \
  $PKG/backend/executor/complex_executor.cpp \
  -I$PKG/backend/executor \
  -O3
check_if_exists $PKG/backend/libexecutor.so
echo ""

echo "Building threaded executor ..."
g++ -std=c++20 -shared -fPIC \
  -o $PKG/backend/libthreadexecutor.so \
  $PKG/backend/executor/simple_executor_threaded.cpp \
  $PKG/backend/executor/xxhash.c \
  $PKG/backend/executor/utils.cpp \
  -I$PKG/backend/executor \
  -O3
check_if_exists $PKG/backend/libthreadexecutor.so
echo ""