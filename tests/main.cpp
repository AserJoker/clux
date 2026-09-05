// clux unit-test entry point.
//
// This file hosts the GoogleTest runner. Individual test suites live in their
// own *.cpp files under tests/ and are globbed into the clux_test target by
// CMake (see the root CMakeLists.txt). Nothing project-specific needs to be
// linked here yet; add libraries to target_link_libraries(clux_test ...) as
// the code under src/ and include/ grows.

#include <gtest/gtest.h>

extern "C" {
#include "icu_data.h"
}

int main(int argc, char **argv) {
  icu_data_init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// Placeholder smoke test so the suite is non-empty and CTest discovery succeeds.
// Real test suites belong in their own tests/*.cpp files.
TEST(Sanity, Smoke) { EXPECT_EQ(1 + 1, 2); }
