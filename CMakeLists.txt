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

# Core library: data types, pipeline, ISP stages and the synthetic RAW
# generator. No OpenCV dependency, so it can be tested without image files.
add_library(minicam_core
  src/core/cfa.cc
  src/core/color.cc
  src/core/metrics.cc
  src/core/status.cc
  src/io/raw_file.cc
  src/io/synthetic.cc
  src/pipeline/frame_pool.cc
  src/pipeline/pipeline.cc
  src/pipeline/threaded_pipeline.cc
  src/stages/black_level.cc
  src/stages/color_correction.cc
  src/stages/demosaic.cc
  src/stages/denoise.cc
  src/stages/tone_map.cc
  src/stages/white_balance.cc
)
target_include_directories(minicam_core PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(minicam_core PUBLIC Threads::Threads PRIVATE minicam_warnings)

# Image file I/O. OpenCV is used here only to decode and encode files.
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs)
add_library(minicam_io src/io/image_io.cc)
target_link_libraries(minicam_io
  PUBLIC minicam_core
  PRIVATE minicam_warnings opencv_core opencv_imgcodecs)
target_include_directories(minicam_io SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})

add_executable(make_synthetic_raw tools/make_synthetic_raw.cc)
target_link_libraries(make_synthetic_raw PRIVATE minicam_io minicam_warnings)

add_executable(minicam tools/minicam_main.cc)
target_link_libraries(minicam PRIVATE minicam_io minicam_warnings)

add_executable(demosaic_eval tools/demosaic_eval.cc)
target_link_libraries(demosaic_eval PRIVATE minicam_io minicam_warnings)

if(MINICAM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
