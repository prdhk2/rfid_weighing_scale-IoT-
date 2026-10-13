#include <unity.h>

#include "elapsed.h"

void setUp() {}
void tearDown() {}

void test_plain_interval() {
  TEST_ASSERT_EQUAL_UINT32(0, elapsedMs(1000, 1000));
  TEST_ASSERT_EQUAL_UINT32(500, elapsedMs(1500, 1000));
}

void test_across_millis_wrap() {
  TEST_ASSERT_EQUAL_UINT32(0x200, elapsedMs(0x100, 0xFFFFFF00u));
}

// The field bug: the MQTT task stamped "healthy" a few ms after the loop took
// `now`, and now - since wrapped to ~4.29e9 ms ("mqtt down 71582 min").
void test_timestamp_slightly_in_the_future_is_zero() {
  TEST_ASSERT_EQUAL_UINT32(0, elapsedMs(1000, 1003));
  TEST_ASSERT_EQUAL_UINT32(0, elapsedMs(5, 0xFFFFFFF0u + 30));  // also across the wrap
}

int runAllTests() {
  UNITY_BEGIN();
  RUN_TEST(test_plain_interval);
  RUN_TEST(test_across_millis_wrap);
  RUN_TEST(test_timestamp_slightly_in_the_future_is_zero);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  delay(2000);
  runAllTests();
}
void loop() {}
#else
int main() { return runAllTests(); }
#endif
