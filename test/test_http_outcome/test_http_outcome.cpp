#include <unity.h>

#include <cstring>

#include "http_outcome.h"

using HttpOutcome::Verdict;
using HttpOutcome::classify;
using HttpOutcome::retrySafe;

void setUp() {}
void tearDown() {}

void test_classify_server_answers() {
  TEST_ASSERT_EQUAL(Verdict::OK, classify(200));
  TEST_ASSERT_EQUAL(Verdict::OK, classify(201));
  TEST_ASSERT_EQUAL(Verdict::DUPLICATE, classify(409));
  TEST_ASSERT_EQUAL(Verdict::NOT_FOUND, classify(404));
  TEST_ASSERT_EQUAL(Verdict::REJECTED, classify(422));
  TEST_ASSERT_EQUAL(Verdict::REJECTED, classify(400));
  TEST_ASSERT_EQUAL(Verdict::AUTH, classify(401));
  TEST_ASSERT_EQUAL(Verdict::AUTH, classify(403));
  TEST_ASSERT_EQUAL(Verdict::SERVER_ERROR, classify(500));
  TEST_ASSERT_EQUAL(Verdict::SERVER_ERROR, classify(503));
  // An http:// URL answered with a redirect to https:// is not a success.
  TEST_ASSERT_EQUAL(Verdict::SERVER_ERROR, classify(301));
  TEST_ASSERT_EQUAL(Verdict::SERVER_ERROR, classify(302));
}

void test_classify_local_failures() {
  TEST_ASSERT_EQUAL(Verdict::NO_WIFI, classify(HttpOutcome::NO_WIFI));
  TEST_ASSERT_EQUAL(Verdict::TRANSPORT, classify(-1));
  TEST_ASSERT_EQUAL(Verdict::TRANSPORT, classify(-11));
  TEST_ASSERT_EQUAL(Verdict::TRANSPORT, classify(0));
}

void test_retry_only_when_request_never_reached_server() {
  TEST_ASSERT_TRUE(retrySafe(-1));   // connect failed
  TEST_ASSERT_TRUE(retrySafe(-2));   // headers not sent
  TEST_ASSERT_TRUE(retrySafe(-3));   // body incomplete
  TEST_ASSERT_TRUE(retrySafe(-4));   // not connected
  TEST_ASSERT_FALSE(retrySafe(-5));  // connection lost after sending: maybe stored
  TEST_ASSERT_FALSE(retrySafe(-11)); // read timeout: maybe stored
  TEST_ASSERT_FALSE(retrySafe(HttpOutcome::NO_WIFI));
  TEST_ASSERT_FALSE(retrySafe(200));
  TEST_ASSERT_FALSE(retrySafe(500));
  TEST_ASSERT_FALSE(retrySafe(409));
}

void test_lcd_texts_fit_the_display() {
  const Verdict all[] = {Verdict::OK, Verdict::DUPLICATE, Verdict::NOT_FOUND, Verdict::REJECTED,
                         Verdict::AUTH, Verdict::SERVER_ERROR, Verdict::TRANSPORT, Verdict::NO_WIFI};
  for (Verdict v : all) {
    TEST_ASSERT_TRUE(strlen(HttpOutcome::lcdTitle(v)) <= 14);   // helvB12 across 128 px
    TEST_ASSERT_TRUE(strlen(HttpOutcome::lcdDetail(v, false)) <= 20);  // 6x10 inside a 122 px frame
    TEST_ASSERT_TRUE(strlen(HttpOutcome::lcdDetail(v, true)) <= 20);
    TEST_ASSERT_TRUE(strlen(HttpOutcome::lcdDetail(v, true)) > 0);
  }
}

void test_not_found_means_different_things_in_and_out() {
  TEST_ASSERT_NOT_EQUAL(0, strcmp(HttpOutcome::lcdDetail(Verdict::NOT_FOUND, false),
                                  HttpOutcome::lcdDetail(Verdict::NOT_FOUND, true)));
}

int runAllTests() {
  UNITY_BEGIN();
  RUN_TEST(test_classify_server_answers);
  RUN_TEST(test_classify_local_failures);
  RUN_TEST(test_retry_only_when_request_never_reached_server);
  RUN_TEST(test_lcd_texts_fit_the_display);
  RUN_TEST(test_not_found_means_different_things_in_and_out);
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
