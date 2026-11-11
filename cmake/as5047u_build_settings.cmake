#===============================================================================
# AS5047U driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(AS5047U_TARGET_NAME "as5047u")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(AS5047U_VERSION_MAJOR 0)
set(AS5047U_VERSION_MINOR 1)
set(AS5047U_VERSION_PATCH 0)
set(AS5047U_VERSION "${AS5047U_VERSION_MAJOR}.${AS5047U_VERSION_MINOR}.${AS5047U_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(AS5047U_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/as5047u_version.h.in")
set(AS5047U_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/as5047u_generated")
set(AS5047U_VERSION_HEADER "${AS5047U_VERSION_HEADER_DIR}/as5047u_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${AS5047U_VERSION_HEADER_DIR}")
configure_file("${AS5047U_VERSION_TEMPLATE}" "${AS5047U_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(AS5047U_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${AS5047U_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/as5047u.ipp, included by inc/as5047u.hpp.
set(AS5047U_SOURCE_FILES)

set(AS5047U_IDF_REQUIRES)
