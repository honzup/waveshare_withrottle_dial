#include <unity.h>
#include "recents_serialize.h"

void test_serialize_roundtrip() {
    std::vector<LocoRef> in = {
        {3,   'S', "Big Boy"},
        {1234,'L', "Shay"},
    };
    std::string s = recents_serialize(in);
    std::vector<LocoRef> out = recents_deserialize(s);
    TEST_ASSERT_EQUAL_INT(2, out.size());
    TEST_ASSERT_EQUAL_INT(3, out[0].address);
    TEST_ASSERT_EQUAL_CHAR('S', out[0].length);
    TEST_ASSERT_EQUAL_STRING("Big Boy", out[0].name.c_str());
    TEST_ASSERT_EQUAL_INT(1234, out[1].address);
}
void test_deserialize_empty_is_empty() {
    TEST_ASSERT_EQUAL_INT(0, recents_deserialize("").size());
}
void test_push_dedups_by_address_and_length() {
    std::vector<LocoRef> list;
    recents_push(list, {3,'S',"Big Boy"}, 5);
    recents_push(list, {1234,'L',"Shay"}, 5);
    recents_push(list, {3,'S',"Big Boy II"}, 5);
    TEST_ASSERT_EQUAL_INT(2, list.size());
    TEST_ASSERT_EQUAL_INT(3, list[0].address);
    TEST_ASSERT_EQUAL_STRING("Big Boy II", list[0].name.c_str());
    TEST_ASSERT_EQUAL_INT(1234, list[1].address);
}
void test_push_trims_to_max() {
    std::vector<LocoRef> list;
    for (int i = 1; i <= 7; ++i) recents_push(list, {i,'L',"n"}, 5);
    TEST_ASSERT_EQUAL_INT(5, list.size());
    TEST_ASSERT_EQUAL_INT(7, list[0].address);
    TEST_ASSERT_EQUAL_INT(3, list[4].address);
}
void test_short_address_distinct_from_long() {
    std::vector<LocoRef> list;
    recents_push(list, {3,'S',"a"}, 5);
    recents_push(list, {3,'L',"b"}, 5);
    TEST_ASSERT_EQUAL_INT(2, list.size());
}
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_roundtrip);
    RUN_TEST(test_deserialize_empty_is_empty);
    RUN_TEST(test_push_dedups_by_address_and_length);
    RUN_TEST(test_push_trims_to_max);
    RUN_TEST(test_short_address_distinct_from_long);
    return UNITY_END();
}
