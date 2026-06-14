#include <unity.h>
#include "horn_resolver.h"
#include <vector>
#include <string>

void test_finds_horn_label() {
    std::vector<std::string> names = {"Light", "Bell", "Horn", "Coupler"};
    TEST_ASSERT_EQUAL_INT(2, resolve_horn_fn(names));
}
void test_case_insensitive() {
    std::vector<std::string> names = {"", "AIR HORN", ""};
    TEST_ASSERT_EQUAL_INT(1, resolve_horn_fn(names));
}
void test_defaults_to_two_when_absent() {
    std::vector<std::string> names = {"Light", "Bell", "", "Steam"};
    TEST_ASSERT_EQUAL_INT(2, resolve_horn_fn(names));
}
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_finds_horn_label);
    RUN_TEST(test_case_insensitive);
    RUN_TEST(test_defaults_to_two_when_absent);
    return UNITY_END();
}
