cmake_minimum_required(VERSION 3.10)

# Build the C++ delocate tool and test runner.
# This is a build-time host tool, so we can use C++17.

add_executable(delocate_cc
  delocate.cc
  parser.cc
  transform.cc
  ar.cc
)
target_compile_features(delocate_cc PRIVATE cxx_std_17)

add_executable(delocate_test_cc
  delocate_test.cc
  parser.cc
  transform.cc
  ar.cc
)
target_compile_features(delocate_test_cc PRIVATE cxx_std_17)
