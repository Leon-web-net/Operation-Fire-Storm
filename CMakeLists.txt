cmake_minimum_required(VERSION 3.16)
project(firestorm CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(firestorm_core STATIC
  src/ctfs.cpp
  src/parser.cpp
  src/reassembler.cpp
)
target_include_directories(firestorm_core PUBLIC src)

add_executable(firestorm
  src/main.cpp
  src/net.cpp
)
target_link_libraries(firestorm PRIVATE firestorm_core)

# Unit tests: ctest --test-dir build/release --output-on-failure
enable_testing()

add_executable(test_parser unit_tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE firestorm_core)
add_test(NAME parser COMMAND test_parser)

add_executable(test_reassembler unit_tests/test_reassembler.cpp)
target_link_libraries(test_reassembler PRIVATE firestorm_core)
add_test(NAME reassembler COMMAND test_reassembler)

# Golden test: the real stage-1 capture must rebuild expected.pdf exactly
add_executable(test_golden unit_tests/test_golden.cpp)
target_link_libraries(test_golden PRIVATE firestorm_core)
target_compile_definitions(test_golden PRIVATE DATA_DIR="${CMAKE_SOURCE_DIR}/unit_tests/data")
add_test(NAME golden COMMAND test_golden)

# Recovery test: rebuilds the crash spool unit_tests/data/1.spool into <project>/1.partial.pdf
add_executable(test_recover unit_tests/test_recover.cpp)
target_link_libraries(test_recover PRIVATE firestorm_core)
target_compile_definitions(test_recover PRIVATE
  DATA_DIR="${CMAKE_SOURCE_DIR}/unit_tests/data" PROJECT_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME recover COMMAND test_recover)
