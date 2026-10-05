include(FetchContent)

# Pinned so local builds and CI use the same version.
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(minicam_tests
  cfa_test.cc
  image_test.cc
  pipeline_test.cc
  status_test.cc
)
target_link_libraries(minicam_tests PRIVATE minicam_core minicam_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(minicam_tests)
