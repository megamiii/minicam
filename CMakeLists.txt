cmake_minimum_required(VERSION 3.20)
project(minicam VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MINICAM_BUILD_TESTS "Build the GoogleTest unit tests" ON)
option(MINICAM_WERROR "Treat compiler warnings as errors" OFF)
set(MINICAM_SANITIZER "" CACHE STRING "Sanitizer to build with: address, thread, or empty")

# Executables go to the top of the build directory (./build/minicam).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Sanitizers are applied to every target, including third-party code, because
# ThreadSanitizer reports false positives when only part of a program is
# instrumented.
if(MINICAM_SANITIZER STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
elseif(MINICAM_SANITIZER STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
elseif(NOT MINICAM_SANITIZER STREQUAL "")
  message(FATAL_ERROR "Unknown MINICAM_SANITIZER '${MINICAM_SANITIZER}'")
endif()

# Warning flags, applied to minicam's own targets only.
add_library(minicam_warnings INTERFACE)
target_compile_options(minicam_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
if(MINICAM_WERROR)
  target_compile_options(minicam_warnings INTERFACE -Werror)
endif()

# Core library: data types, pipeline and ISP stages. No OpenCV dependency.
add_library(minicam_core
  src/core/cfa.cc
  src/core/status.cc
  src/pipeline/pipeline.cc
)
target_include_directories(minicam_core PUBLIC include)
target_link_libraries(minicam_core PRIVATE minicam_warnings)

if(MINICAM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
