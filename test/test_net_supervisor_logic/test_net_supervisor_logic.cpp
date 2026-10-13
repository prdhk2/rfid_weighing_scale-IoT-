#include <unity.h>

#include "net_supervisor_logic.h"

using NetSupervisor::Action;
using NetSupervisor::MINUTE_MS;

void setUp() {}
void tearDown() {}

void test_threshold_backs_off_and_caps() {
  TEST_ASSERT_EQUAL_UINT32(15 * MINUTE_MS, NetSupervisor::offlineThresholdMs(0));
  TEST_ASSERT_EQUAL_UINT32(30 * MINUTE_MS, NetSupervisor::offlineThresholdMs(1));
  TEST_ASSERT_EQUAL_UINT32(60 * MINUTE_MS, NetSupervisor::offlineThresholdMs(2));
  TEST_ASSERT_EQUAL_UINT32(120 * MINUTE_MS, NetSupervisor::offlineThresholdMs(3));
  TEST_ASSERT_EQUAL_UINT32(120 * MINUTE_MS, NetSupervisor::offlineThresholdMs(200));
}

void test_no_action_below_threshold() {
  TEST_ASSERT_EQUAL(Action::NONE, NetSupervisor::decide(0, false, 0));
  TEST_ASSERT_EQUAL(Action::NONE, NetSupervisor::decide(15 * MINUTE_MS - 1, false, 0));
  TEST_ASSERT_EQUAL(Action::NONE, NetSupervisor::decide(29 * MINUTE_MS, false, 1));
}

void test_restarts_when_idle_past_threshold() {
  TEST_ASSERT_EQUAL(Action::RESTART, NetSupervisor::decide(15 * MINUTE_MS, false, 0));
  TEST_ASSERT_EQUAL(Action::RESTART, NetSupervisor::decide(120 * MINUTE_MS, false, 5));
}

void test_waits_for_idle_then_forces() {
  TEST_ASSERT_EQUAL(Action::NONE, NetSupervisor::decide(15 * MINUTE_MS, true, 0));
  TEST_ASSERT_EQUAL(Action::NONE, NetSupervisor::decide(45 * MINUTE_MS - 1, true, 0));
  TEST_ASSERT_EQUAL(Action::RESTART_FORCED, NetSupervisor::decide(45 * MINUTE_MS, true, 0));
}

int runAllTests() {
  UNITY_BEGIN();
  RUN_TEST(test_threshold_backs_off_and_caps);
  RUN_TEST(test_no_action_below_threshold);
  RUN_TEST(test_restarts_when_idle_past_threshold);
  RUN_TEST(test_waits_for_idle_then_forces);
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
