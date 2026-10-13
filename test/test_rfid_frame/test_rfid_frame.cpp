#include <unity.h>

#include <cstring>

#include "rfid_frame.h"

using RfidFrame::Assembler;
using RfidFrame::Card;
using RfidFrame::Direction;
using RfidFrame::ParseResult;
using RfidFrame::parsePayload;

void setUp() {}
void tearDown() {}

Assembler::Event feed(Assembler &a, const char *bytes, size_t len) {
  Assembler::Event last = Assembler::Event::NONE;
  for (size_t i = 0; i < len; ++i) {
    Assembler::Event e = a.push(static_cast<uint8_t>(bytes[i]));
    if (e != Assembler::Event::NONE) last = e;
  }
  return last;
}

// ---------------------------------------------------------------- framing

void test_frame_between_stx_and_etx() {
  Assembler a;
  const char bytes[] = "noise\x02" "1,12,Budi,3,B 1234 XY,PT Maju" "\x03" "tail";
  TEST_ASSERT_EQUAL(Assembler::Event::FRAME, feed(a, bytes, sizeof(bytes) - 1));
  TEST_ASSERT_EQUAL_STRING("1,12,Budi,3,B 1234 XY,PT Maju", a.payload());
  TEST_ASSERT_FALSE(a.receiving());
}

void test_non_printable_bytes_inside_frame_are_dropped() {
  Assembler a;
  const char bytes[] = "\x02" "1,\x01" "2\xff,A,3,B,C" "\x03";
  TEST_ASSERT_EQUAL(Assembler::Event::FRAME, feed(a, bytes, sizeof(bytes) - 1));
  TEST_ASSERT_EQUAL_STRING("1,2,A,3,B,C", a.payload());
}

void test_new_stx_restarts_frame() {
  Assembler a;
  const char bytes[] = "\x02" "garbage" "\x02" "2,5,X,6,Y,Z" "\x03";
  TEST_ASSERT_EQUAL(Assembler::Event::FRAME, feed(a, bytes, sizeof(bytes) - 1));
  TEST_ASSERT_EQUAL_STRING("2,5,X,6,Y,Z", a.payload());
}

void test_overflow_is_reported_and_dropped() {
  Assembler a;
  a.push(0x02);
  Assembler::Event last = Assembler::Event::NONE;
  for (size_t i = 0; i < RfidFrame::MAX_PAYLOAD + 1; ++i) {
    Assembler::Event e = a.push('A');
    if (e != Assembler::Event::NONE) last = e;
  }
  TEST_ASSERT_EQUAL(Assembler::Event::OVERFLOW, last);
  TEST_ASSERT_FALSE(a.receiving());
  TEST_ASSERT_EQUAL(Assembler::Event::NONE, a.push(0x03));  // ETX after overflow: nothing
}

void test_etx_without_stx_is_ignored() {
  Assembler a;
  TEST_ASSERT_EQUAL(Assembler::Event::NONE, a.push(0x03));
}

// ---------------------------------------------------------------- payload

void test_parses_entry_card() {
  Card c;
  TEST_ASSERT_EQUAL(ParseResult::OK, parsePayload("1,12,Budi Santoso,3,B 1234 XY,PT Maju", c));
  TEST_ASSERT_EQUAL(Direction::IN, c.direction);
  TEST_ASSERT_EQUAL_STRING("1", c.slaveId);
  TEST_ASSERT_EQUAL_INT32(12, c.driverId);
  TEST_ASSERT_EQUAL_STRING("Budi Santoso", c.driverName);
  TEST_ASSERT_EQUAL_INT32(3, c.customerId);
  TEST_ASSERT_EQUAL_STRING("B 1234 XY", c.plate);
  TEST_ASSERT_EQUAL_STRING("PT Maju", c.customerName);
}

void test_parses_exit_card_and_trims() {
  Card c;
  TEST_ASSERT_EQUAL(ParseResult::OK, parsePayload(" 2 , 7 ,  Ani , 9 , F 1 A ,  CV X  ", c));
  TEST_ASSERT_EQUAL(Direction::OUT, c.direction);
  TEST_ASSERT_EQUAL_INT32(7, c.driverId);
  TEST_ASSERT_EQUAL_STRING("Ani", c.driverName);
  TEST_ASSERT_EQUAL_INT32(9, c.customerId);
  TEST_ASSERT_EQUAL_STRING("F 1 A", c.plate);
  TEST_ASSERT_EQUAL_STRING("CV X", c.customerName);
}

void test_customer_name_keeps_commas() {
  Card c;
  TEST_ASSERT_EQUAL(ParseResult::OK, parsePayload("1,1,A,2,B,PT Maju, Tbk", c));
  TEST_ASSERT_EQUAL_STRING("PT Maju, Tbk", c.customerName);
}

void test_empty_names_allowed() {
  Card c;
  TEST_ASSERT_EQUAL(ParseResult::OK, parsePayload("1,1,,2,,", c));
  TEST_ASSERT_EQUAL_STRING("", c.driverName);
  TEST_ASSERT_EQUAL_STRING("", c.plate);
  TEST_ASSERT_EQUAL_STRING("", c.customerName);
}

void test_rejects_too_few_fields() {
  Card c;
  TEST_ASSERT_EQUAL(ParseResult::BAD_FIELD_COUNT, parsePayload("1,12,Budi,3,B 1234", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_FIELD_COUNT, parsePayload("", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_FIELD_COUNT, parsePayload(nullptr, c));
}

void test_rejects_unknown_slave() {
  Card c;
  TEST_ASSERT_EQUAL(ParseResult::BAD_SLAVE, parsePayload("3,12,Budi,3,B,C", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_SLAVE, parsePayload("12,12,Budi,3,B,C", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_SLAVE, parsePayload(",12,Budi,3,B,C", c));
}

void test_rejects_bad_ids() {
  Card c;
  TEST_ASSERT_EQUAL(ParseResult::BAD_DRIVER_ID, parsePayload("1,0,A,3,B,C", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_DRIVER_ID, parsePayload("1,-4,A,3,B,C", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_DRIVER_ID, parsePayload("1,12x,A,3,B,C", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_DRIVER_ID, parsePayload("1,,A,3,B,C", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_DRIVER_ID, parsePayload("1,99999999999,A,3,B,C", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_CUSTOMER_ID, parsePayload("1,5,A,x,B,C", c));
  TEST_ASSERT_EQUAL(ParseResult::BAD_CUSTOMER_ID, parsePayload("1,5,A,1.5,B,C", c));
}

void test_output_untouched_on_failure() {
  Card c;
  memset(&c, 0, sizeof(c));
  strcpy(c.plate, "KEEP");
  TEST_ASSERT_NOT_EQUAL(ParseResult::OK, parsePayload("1,0,A,3,B,C", c));
  TEST_ASSERT_EQUAL_STRING("KEEP", c.plate);
}

void test_long_text_is_cut_and_json_safe() {
  Card c;
  TEST_ASSERT_EQUAL(ParseResult::OK,
                    parsePayload("1,1,A\"B\\C,2,ABCDEFGHIJKLMNOPQRSTUVWXYZ,"
                                 "0123456789012345678901234567890123456789", c));
  TEST_ASSERT_EQUAL_STRING("A'B'C", c.driverName);
  TEST_ASSERT_EQUAL(RfidFrame::MAX_PLATE, strlen(c.plate));
  TEST_ASSERT_EQUAL(RfidFrame::MAX_NAME, strlen(c.customerName));
}

int runAllTests() {
  UNITY_BEGIN();
  RUN_TEST(test_frame_between_stx_and_etx);
  RUN_TEST(test_non_printable_bytes_inside_frame_are_dropped);
  RUN_TEST(test_new_stx_restarts_frame);
  RUN_TEST(test_overflow_is_reported_and_dropped);
  RUN_TEST(test_etx_without_stx_is_ignored);
  RUN_TEST(test_parses_entry_card);
  RUN_TEST(test_parses_exit_card_and_trims);
  RUN_TEST(test_customer_name_keeps_commas);
  RUN_TEST(test_empty_names_allowed);
  RUN_TEST(test_rejects_too_few_fields);
  RUN_TEST(test_rejects_unknown_slave);
  RUN_TEST(test_rejects_bad_ids);
  RUN_TEST(test_output_untouched_on_failure);
  RUN_TEST(test_long_text_is_cut_and_json_safe);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  delay(2000);  // let the serial monitor attach
  runAllTests();
}
void loop() {}
#else
int main() { return runAllTests(); }
#endif
