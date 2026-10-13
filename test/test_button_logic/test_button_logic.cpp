#include <unity.h>

#include "button_logic.h"

void setUp() {}
void tearDown() {}

constexpr uint32_t DEBOUNCE = 50;
constexpr uint32_t LONG = 3000;

// Holds `level` from `from` to `to` in 10 ms steps; ORs the events seen.
DebouncedButton::Events hold(DebouncedButton &b, bool level, uint32_t from, uint32_t to) {
  DebouncedButton::Events all;
  for (uint32_t t = from; t <= to; t += 10) {
    DebouncedButton::Events e = b.update(level, t);
    all.pressed |= e.pressed;
    all.released |= e.released;
    all.longPress |= e.longPress;
  }
  return all;
}

void test_press_and_release_edges_after_debounce() {
  DebouncedButton b(DEBOUNCE, LONG);
  hold(b, false, 0, 100);
  DebouncedButton::Events e = b.update(true, 110);
  TEST_ASSERT_FALSE(e.pressed);  // not yet stable
  e = hold(b, true, 120, 200);
  TEST_ASSERT_TRUE(e.pressed);
  TEST_ASSERT_TRUE(b.isDown());
  e = hold(b, false, 210, 300);
  TEST_ASSERT_TRUE(e.released);
  TEST_ASSERT_FALSE(b.isDown());
}

void test_bounce_shorter_than_debounce_is_ignored() {
  DebouncedButton b(DEBOUNCE, LONG);
  hold(b, false, 0, 100);
  DebouncedButton::Events all;
  for (uint32_t t = 110; t < 300; t += 20) {  // toggles every 20 ms
    DebouncedButton::Events e = b.update(((t / 20) & 1) != 0, t);
    all.pressed |= e.pressed;
  }
  TEST_ASSERT_FALSE(all.pressed);
}

void test_holding_fires_one_press_and_one_long_press() {
  DebouncedButton b(DEBOUNCE, LONG);
  hold(b, false, 0, 100);
  int presses = 0;
  int longs = 0;
  for (uint32_t t = 110; t <= 10000; t += 10) {
    DebouncedButton::Events e = b.update(true, t);
    presses += e.pressed;
    longs += e.longPress;
  }
  TEST_ASSERT_EQUAL(1, presses);
  TEST_ASSERT_EQUAL(1, longs);
}

void test_long_press_timing() {
  DebouncedButton b(DEBOUNCE, LONG);
  hold(b, false, 0, 100);
  DebouncedButton::Events e = hold(b, true, 110, 110 + DEBOUNCE + LONG - 20);
  TEST_ASSERT_FALSE(e.longPress);
  e = hold(b, true, 110 + DEBOUNCE + LONG - 10, 110 + DEBOUNCE + LONG + 20);
  TEST_ASSERT_TRUE(e.longPress);
}

void test_button_down_at_power_up_does_not_fire() {
  DebouncedButton b(DEBOUNCE, LONG);
  DebouncedButton::Events e = hold(b, true, 0, 5000);
  TEST_ASSERT_FALSE(e.pressed);
  TEST_ASSERT_FALSE(e.longPress);
  e = hold(b, false, 5010, 5100);
  TEST_ASSERT_FALSE(e.released);
  e = hold(b, true, 5110, 5200);  // the next real press works
  TEST_ASSERT_TRUE(e.pressed);
}

void test_suppress_until_release() {
  DebouncedButton b(DEBOUNCE, LONG);
  hold(b, false, 0, 100);
  TEST_ASSERT_TRUE(hold(b, true, 110, 200).pressed);
  b.suppressUntilRelease();  // e.g. the screen changed under the finger
  DebouncedButton::Events e = hold(b, true, 210, 5000);
  TEST_ASSERT_FALSE(e.longPress);
  e = hold(b, false, 5010, 5100);
  TEST_ASSERT_FALSE(e.released);
  TEST_ASSERT_TRUE(hold(b, true, 5110, 5200).pressed);
}

void test_suppress_when_idle_does_nothing() {
  DebouncedButton b(DEBOUNCE, LONG);
  hold(b, false, 0, 100);
  b.suppressUntilRelease();
  TEST_ASSERT_TRUE(hold(b, true, 110, 200).pressed);
}

void test_millis_wraparound() {
  DebouncedButton b(DEBOUNCE, LONG);
  const uint32_t start = 0xFFFFFF00u;
  b.update(false, start);
  DebouncedButton::Events all;
  for (uint32_t i = 0; i < 100; ++i) {
    DebouncedButton::Events e = b.update(true, start + 10 + i * 10);  // wraps past 0
    all.pressed |= e.pressed;
  }
  TEST_ASSERT_TRUE(all.pressed);
}

int runAllTests() {
  UNITY_BEGIN();
  RUN_TEST(test_press_and_release_edges_after_debounce);
  RUN_TEST(test_bounce_shorter_than_debounce_is_ignored);
  RUN_TEST(test_holding_fires_one_press_and_one_long_press);
  RUN_TEST(test_long_press_timing);
  RUN_TEST(test_button_down_at_power_up_does_not_fire);
  RUN_TEST(test_suppress_until_release);
  RUN_TEST(test_suppress_when_idle_does_nothing);
  RUN_TEST(test_millis_wraparound);
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
