cmake_minimum_required(VERSION 3.14)
project(clux C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Compiler warnings
#
# Applied to project targets only — third_party (ICU, GoogleTest) keeps its
# own flags. MSVC-style drivers (cl / clang-cl) get /W4, GNU-style drivers
# get -Wall -Wextra -Wpedantic. Diagnostics that are deliberately not
# followed are disabled in this single place, each with a reason.
# ---------------------------------------------------------------------------
function(clux_target_warnings target)
  # clux deliberately uses the portable C API (fopen, tmpnam, ...) instead of
  # the MSVC-only *_s variants, so silence that family of CRT diagnostics.
  if(WIN32)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_subdirectory(third_party/icu)

add_executable(clux src/main.c)
clux_target_warnings(clux)

# ---------------------------------------------------------------------------
# Core library (allocator + runtime support)
# ---------------------------------------------------------------------------
add_library(clux_core STATIC src/core/allocator.c src/core/arena.c src/core/panic.c src/core/vec.c src/core/rbtree.c src/core/omap.c src/core/strmap.c src/core/stream.c src/core/string.c)
target_include_directories(clux_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(clux_core PUBLIC ICU::uc ICU::i18n)
clux_target_warnings(clux_core)

# ---------------------------------------------------------------------------
# Parser library (tokenizer + ast parser)
#
# Only src/parser/lexer.c exists so far; src/parser/parser.c (T4) is globbed
# in automatically once it lands.
# ---------------------------------------------------------------------------
file(GLOB CLUX_PARSER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/parser/*.c")
add_library(clux_parser STATIC ${CLUX_PARSER_SOURCES})
target_include_directories(clux_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(clux_parser PUBLIC clux_core ICU::uc ICU::i18n)
clux_target_warnings(clux_parser)

# ---------------------------------------------------------------------------
# VM library (virtual machine: value, scope, type, function)
# ---------------------------------------------------------------------------
file(GLOB CLUX_VM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/*.c")
add_library(clux_vm STATIC ${CLUX_VM_SOURCES})
target_include_directories(clux_vm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(clux_vm PUBLIC clux_core clux_parser ICU::uc ICU::i18n)
clux_target_warnings(clux_vm)

# ---------------------------------------------------------------------------
# Semantic analysis + diagnostics
#
# src/sema and src/diag are still empty (pipeline stages T5/T6). These
# targets materialize as soon as the first *.c file lands in the directory,
# so no CMake edit is needed when they are added.
# ---------------------------------------------------------------------------
set(CLUX_STAGE_LIBS "")

file(GLOB CLUX_SEMA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sema/*.c")
if(CLUX_SEMA_SOURCES)
  add_library(clux_sema STATIC ${CLUX_SEMA_SOURCES})
  target_include_directories(clux_sema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(clux_sema PUBLIC clux_core ICU::uc ICU::i18n)
  clux_target_warnings(clux_sema)
  list(APPEND CLUX_STAGE_LIBS clux_sema)
endif()

file(GLOB CLUX_DIAG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/diag/*.c")
if(CLUX_DIAG_SOURCES)
  add_library(clux_diag STATIC ${CLUX_DIAG_SOURCES})
  target_include_directories(clux_diag PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(clux_diag PUBLIC clux_core)
  clux_target_warnings(clux_diag)
  list(APPEND CLUX_STAGE_LIBS clux_diag)
endif()

# ---------------------------------------------------------------------------
# Driver library (pipeline orchestration: load -> lex -> ...)
#
# Only src/driver/driver.c exists so far; later stages (parser/sema/interp)
# land here as additional *.c files (GLOB collects them automatically).
# ---------------------------------------------------------------------------
file(GLOB CLUX_DRIVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/driver/*.c")
add_library(clux_driver STATIC ${CLUX_DRIVER_SOURCES})
target_include_directories(clux_driver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(clux_driver PUBLIC clux_core clux_parser clux_vm ICU::uc ICU::i18n)
clux_target_warnings(clux_driver)

# ---------------------------------------------------------------------------
# Command library (subcommand dispatch framework)
# ---------------------------------------------------------------------------
add_library(clux_cmd STATIC
    src/cmd/cmd.c
    src/cmd/format.c
    src/cmd/build.c
    src/cmd/run.c
    src/cmd/test.c
    src/cmd/version.c
)
target_include_directories(clux_cmd PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(clux_cmd PUBLIC clux_core clux_driver ICU::uc icu_data_init)
clux_target_warnings(clux_cmd)

target_link_libraries(clux PRIVATE
    clux_cmd
    clux_core
    clux_parser
    clux_driver
    clux_vm
    ${CLUX_STAGE_LIBS}
    ICU::data
    ICU::io
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
  target_include_directories(clux_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_link_libraries(clux_test PRIVATE clux_core clux_cmd clux_parser clux_driver clux_vm ${CLUX_STAGE_LIBS} icu_data_init ICU::data GTest::gtest)
  clux_target_warnings(clux_test)

  include(GoogleTest)
  gtest_discover_tests(clux_test)
endif()
