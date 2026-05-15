cmake_minimum_required(VERSION 3.22)

project(flexrml VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FLEXRML_VCPKG_TRIPLET "$ENV{VCPKG_TRIPLET}")
if(NOT FLEXRML_VCPKG_TRIPLET)
  set(FLEXRML_VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
endif()
if(NOT FLEXRML_VCPKG_TRIPLET)
  set(FLEXRML_VCPKG_TRIPLET "x64-linux")
endif()

set(FLEXRML_LOCAL_VCPKG_PREFIX
    "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${FLEXRML_VCPKG_TRIPLET}")
if(EXISTS "${FLEXRML_LOCAL_VCPKG_PREFIX}")
  list(PREPEND CMAKE_PREFIX_PATH "${FLEXRML_LOCAL_VCPKG_PREFIX}")
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(jsoncons CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(unordered_dense CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
pkg_check_modules(SERD REQUIRED IMPORTED_TARGET serd-0)

configure_file(
  src/flexrml/version.h.in
  generated/version.h
  @ONLY
)

set(FLEXRML_SOURCES
  src/flexrml/main.cpp
  src/flexrml/cli.cpp
  src/flexrml/pipeline.cpp
  src/flexrml/backend/planner/backend_planner.cpp
  src/flexrml/backend/optimizer/ra_expression_partitioner.cpp
  src/flexrml/backend/source/csv_source_reader.cpp
  src/flexrml/backend/source/json/json_preprocessor.cpp
  src/flexrml/backend/source/json_source_reader.cpp
  src/flexrml/backend/source/source_reader_factory.cpp
  src/flexrml/backend/source/xml_source_reader.cpp
  src/flexrml/backend/executor/csv_row.cpp
  src/flexrml/backend/executor/executor.cpp
  src/flexrml/backend/executor/simple_executor.cpp
  src/flexrml/backend/program/join_program.cpp
  src/flexrml/backend/program/simple_program.cpp
  src/flexrml/backend/program/term_program.cpp
  src/flexrml/backend/executor/utils.cpp
  src/flexrml/backend/executor/complex_executor.cpp
  src/flexrml/frontend/rdf_parser/rdf_parser_lib.cpp
  src/flexrml/frontend/rdf_parser/rdf_parser.cpp
  src/flexrml/frontend/rml_normalizer/rml_core_normalizer.cpp
  src/flexrml/frontend/functions/rml_functions.cpp
  src/flexrml/frontend/ra_converter/ra_converter_rml_core.cpp
)

add_executable(flexrml ${FLEXRML_SOURCES})

target_include_directories(flexrml PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  src/flexrml
  src/flexrml/backend/planner
  src/flexrml/backend/program
  src/flexrml/backend/source
  src/flexrml/backend/source/json
  src/flexrml/backend/executor
)

target_link_libraries(flexrml PRIVATE
  jsoncons::jsoncons
  pugixml::pugixml
  unordered_dense::unordered_dense
  xxHash::xxhash
  PkgConfig::SERD
  Threads::Threads
  m
)

set_target_properties(flexrml PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(FLEXRML_VALIDATOR_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/env/bin/python")
if(NOT EXISTS "${FLEXRML_VALIDATOR_PYTHON}")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(FLEXRML_VALIDATOR_PYTHON "${Python3_EXECUTABLE}")
endif()

add_custom_target(validate
  COMMAND "${FLEXRML_VALIDATOR_PYTHON}" scripts/validate_test_cases.py
  DEPENDS flexrml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Running FlexRML validation test cases"
  VERBATIM
)

add_custom_target(benchmark
  COMMAND "${FLEXRML_VALIDATOR_PYTHON}" scripts/run_benchmarks.py
  DEPENDS flexrml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Running FlexRML benchmarks"
  VERBATIM
)
