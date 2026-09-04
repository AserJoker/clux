#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

extern "C" {
#include "core/panic.h"
}

/* ---- Test helper: throw-based handler for C++ catch ---- */

static thread_local std::string g_last_panic_message;

extern "C" void throw_panic_handler(const char *message) {
  g_last_panic_message = message;
  throw std::runtime_error(message);
}

/* A handler that records the message without terminating. */
static thread_local int g_record_count = 0;
static thread_local char g_recorded_message[1024];

extern "C" void record_panic_handler(const char *message) {
  g_record_count++;
  snprintf(g_recorded_message, sizeof(g_recorded_message), "%s", message);
  throw std::runtime_error(message);
}

/* ---- Fixture: save/restore panic handler ---- */

class PanicTest : public ::testing::Test {
protected:
  panic_handler_t saved_;

  void SetUp() override { saved_ = get_panic_handler(); }

  void TearDown() override { set_panic_handler(saved_); }
};

/* ---- PanicHandler ---- */

TEST_F(PanicTest, DefaultHandlerIsAbort) {
  EXPECT_EQ(get_panic_handler(), panic_handler_abort);
}

TEST_F(PanicTest, SetGetHandler) {
  set_panic_handler(throw_panic_handler);
  EXPECT_EQ(get_panic_handler(), throw_panic_handler);
}

TEST_F(PanicTest, SetNullHandlerIgnored) {
  panic_handler_t before = get_panic_handler();
  set_panic_handler(nullptr);
  EXPECT_EQ(get_panic_handler(), before); // unchanged
}

/* ---- panic() with throw handler ---- */

TEST_F(PanicTest, PanicThrowsViaHandler) {
  set_panic_handler(throw_panic_handler);

  try {
    panic("hello %s %d", "world", 42);
    FAIL() << "panic should not return";
  } catch (const std::runtime_error &e) {
    EXPECT_STREQ(e.what(), "hello world 42");
  }
}

TEST_F(PanicTest, PanicLongMessage) {
  set_panic_handler(throw_panic_handler);

  // Message longer than 1024 bytes should be truncated, not crash
  try {
    panic("%1024d", 1); // force a long format
    FAIL() << "panic should not return";
  } catch (const std::runtime_error &e) {
    // Just verify it didn't crash; content may be truncated
    SUCCEED();
  }
}

/* ---- assert macro ---- */

TEST_F(PanicTest, AssertPassDoesNotPanic) {
  set_panic_handler(throw_panic_handler);

  // True condition — no throw
  ASSERT_NO_THROW(assert(1 == 1, "should not fire"));
}

TEST_F(PanicTest, AssertFailPanics) {
  set_panic_handler(throw_panic_handler);

  try {
    assert(0 != 0, "zero equals zero");
    FAIL() << "assert(false) should not return";
  } catch (const std::runtime_error &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("assertion failed"), std::string::npos);
    EXPECT_NE(msg.find("0 != 0"), std::string::npos);
    EXPECT_NE(msg.find("zero equals zero"), std::string::npos);
    // Should contain file:line info
    EXPECT_NE(msg.find(":"), std::string::npos);
  }
}

TEST_F(PanicTest, AssertWithExpression) {
  set_panic_handler(throw_panic_handler);

  int x = 5;
  try {
    assert(x > 10, "x too small");
    FAIL() << "assert should have fired";
  } catch (const std::runtime_error &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("assertion failed"), std::string::npos);
    EXPECT_NE(msg.find("x > 10"), std::string::npos);
    EXPECT_NE(msg.find("x too small"), std::string::npos);
  }
}

/* ---- assert uses panic, which uses current handler ---- */

TEST_F(PanicTest, AssertUsesCurrentHandler) {
  g_record_count = 0;
  set_panic_handler(record_panic_handler);

  try {
    assert(false, "handler check");
  } catch (const std::runtime_error &) {
    // expected
  }

  EXPECT_EQ(g_record_count, 1);
  EXPECT_NE(std::string(g_recorded_message).find("handler check"),
            std::string::npos);
}

/* ---- Multiple sequential panics ---- */

TEST_F(PanicTest, SequentialPanics) {
  set_panic_handler(throw_panic_handler);

  try {
    panic("first");
  } catch (const std::runtime_error &e) {
    EXPECT_STREQ(e.what(), "first");
  }

  try {
    panic("second %d", 2);
  } catch (const std::runtime_error &e) {
    EXPECT_STREQ(e.what(), "second 2");
  }
}

/* ---- Handler can be switched back to abort ---- */

TEST_F(PanicTest, RestoreAbortHandler) {
  set_panic_handler(throw_panic_handler);
  EXPECT_EQ(get_panic_handler(), throw_panic_handler);

  set_panic_handler(panic_handler_abort);
  EXPECT_EQ(get_panic_handler(), panic_handler_abort);

  // Switch back for subsequent tests
  set_panic_handler(throw_panic_handler);
  EXPECT_EQ(get_panic_handler(), throw_panic_handler);
}
