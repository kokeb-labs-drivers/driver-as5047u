# =============================================================================
# AS5047U driver: host example (any PC, no hardware)
# =============================================================================
# The driver against a simulated AS5047U (the register model the host tests
# use, tests/mock_as5047u.hpp) through the host port in this folder.
#
#   cmake -S examples/host -B build-example
#   cmake --build build-example && ./build-example/as5047u_host_example
#
# The host tests also build this folder and run the example (CTest
# as5047u_host_example), so it always compiles against the current headers.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(as5047u_host_example LANGUAGES CXX)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. as5047u)
endif()

# ISO C++20, also when this folder is built on its own (handbook KB-CPP20-001).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(as5047u_host_example main.cpp)
target_link_libraries(as5047u_host_example PRIVATE as5047u::as5047u)
target_include_directories(as5047u_host_example PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../../tests)
target_compile_features(as5047u_host_example PRIVATE cxx_std_20)
target_compile_options(as5047u_host_example PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
  -pedantic-errors)
