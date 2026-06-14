#include <unity.h>
#include "loco_ref.h"

void test_parse_short_address() {
    LocoRef r;
    TEST_ASSERT_TRUE(loco_parse_address("S3", r));
    TEST_ASSERT_EQUAL_INT(3, r.address);
    TEST_ASSERT_EQUAL_CHAR('S', r.length);
}
void test_parse_long_address() {
    LocoRef r;
    TEST_ASSERT_TRUE(loco_parse_address("L1234", r));
    TEST_ASSERT_EQUAL_INT(1234, r.address);
    TEST_ASSERT_EQUAL_CHAR('L', r.length);
}
void test_parse_rejects_garbage() {
    LocoRef r;
    TEST_ASSERT_FALSE(loco_parse_address("X", r));
    TEST_ASSERT_FALSE(loco_parse_address("", r));
    TEST_ASSERT_FALSE(loco_parse_address("S", r));
}
void test_format_address_roundtrip() {
    TEST_ASSERT_EQUAL_STRING("S3",   loco_format_address(3, 'S').c_str());
    TEST_ASSERT_EQUAL_STRING("L1234", loco_format_address(1234, 'L').c_str());
}
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_short_address);
    RUN_TEST(test_parse_long_address);
    RUN_TEST(test_parse_rejects_garbage);
    RUN_TEST(test_format_address_roundtrip);
    return UNITY_END();
}
