cmake_minimum_required(VERSION 3.14)
project(clux C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/icu)

add_executable(clux src/main.c)

# ---------------------------------------------------------------------------
# Core library (allocator + runtime support)
# ---------------------------------------------------------------------------
add_library(clux_core STATIC src/core/allocator.c src/core/panic.c src/core/vec.c)
target_include_directories(clux_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(clux PRIVATE
    clux_core
    ICU::i18n
    ICU::uc
    ICU::data
    ICU::io
    icu_data_init
)

# ---------------------------------------------------------------------------
# Unit tests (GoogleTest, introduced as a git submodule at third_party/gtest)
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build the unit test suite" ON)
if(BUILD_TESTS)
  enable_testing()

  # Bring in the GTest::gtest / GTest::gmock targets built from the submodule.
  # Force gtest to use the same (dynamic) MSVC CRT as the rest of the project,
  # otherwise linking the test executable fails with a RuntimeLibrary mismatch
  # (MDd vs MTd) under clang-cl / MSVC. Harmless on Linux (no CRT distinction).
  set(gtest_force_shared_crt ON CACHE BOOL "Use shared (DLL) CRT for gtest" FORCE)
  add_subdirectory(third_party/gtest)

  # Glob every *.cpp under tests/ so new test files are picked up automatically
  # (CONFIGURE_DEPENDS re-runs globbing on each configure when the filesystem changes).
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")

  add_executable(clux_test ${TEST_SOURCES})
  target_link_libraries(clux_test PRIVATE clux_core GTest::gtest)

  include(GoogleTest)
  gtest_discover_tests(clux_test)
endif()