#===============================================================================
# AS5047U driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   as5047u, consumed as as5047u::as5047u both through add_subdirectory() and
#   through find_package(as5047u) after install.
#
#   cmake -S . -B build -DAS5047U_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/as5047u_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${AS5047U_SOURCE_FILES}
        INCLUDE_DIRS ${AS5047U_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${AS5047U_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(AS5047U_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(as5047u_driver
    VERSION ${AS5047U_VERSION}
    DESCRIPTION "C++20 driver for the AS5047U 14-bit magnetic rotary position sensor."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because as5047u.hpp includes
# "../src/as5047u.ipp" relative to itself:
#   <prefix>/include/as5047u/inc/*.hpp, as5047u_version.h
#   <prefix>/include/as5047u/src/as5047u.ipp
set(AS5047U_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/as5047u/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${AS5047U_TARGET_NAME} INTERFACE)
target_include_directories(${AS5047U_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${AS5047U_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${AS5047U_INSTALL_INCLUDEDIR}>
)
target_compile_features(${AS5047U_TARGET_NAME} INTERFACE cxx_std_20)

# as5047u::as5047u everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(as5047u::as5047u ALIAS ${AS5047U_TARGET_NAME})
set_target_properties(${AS5047U_TARGET_NAME} PROPERTIES EXPORT_NAME as5047u)

option(AS5047U_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links as5047u::as5047u" OFF)
if(AS5047U_ENABLE_WARNINGS)
    target_compile_options(${AS5047U_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(AS5047U_BUILD_TESTS "Build the host tests in tests/" OFF)
option(AS5047U_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(AS5047U_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${AS5047U_TARGET_NAME} EXPORT as5047uTargets
    INCLUDES DESTINATION ${AS5047U_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${AS5047U_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${AS5047U_VERSION_HEADER}" DESTINATION ${AS5047U_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/as5047u/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT as5047uTargets
    FILE as5047uTargets.cmake
    NAMESPACE as5047u::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/as5047u)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/as5047uConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/as5047uConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/as5047u)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/as5047uConfigVersion.cmake"
    VERSION ${AS5047U_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/as5047uConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/as5047uConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/as5047u)
