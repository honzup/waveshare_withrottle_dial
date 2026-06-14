#include <unity.h>
#include "jmri_discovery.h"
using namespace jmri_discovery;

void test_empty_with_saved_uses_saved() {
    ChooseResult r = choose({}, "192.168.1.46", 12090);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::UseSaved, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING("192.168.1.46", r.server.ip.c_str());
    TEST_ASSERT_EQUAL_UINT16(12090, r.server.port);
}
void test_empty_without_saved_is_none() {
    ChooseResult r = choose({}, "", 12090);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::None, (int)r.kind);
}
void test_single_uses_discovered() {
    ChooseResult r = choose({{"JMRI", "10.0.0.5", 12090}}, "192.168.1.46", 12090);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::UseDiscovered, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", r.server.ip.c_str());
}
void test_multiple_is_ambiguous() {
    ChooseResult r = choose({{"A","10.0.0.5",12090},{"B","10.0.0.6",12090}}, "", 0);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::Ambiguous, (int)r.kind);
}
void test_multiple_with_saved_match_uses_it() {
    ChooseResult r = choose({{"A","10.0.0.5",12090},{"B","10.0.0.6",12090}}, "10.0.0.6", 12090);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::UseDiscovered, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING("10.0.0.6", r.server.ip.c_str());
}
void test_multiple_with_saved_no_match_is_ambiguous() {
    ChooseResult r = choose({{"A","10.0.0.5",12090},{"B","10.0.0.6",12090}}, "10.0.0.9", 12090);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::Ambiguous, (int)r.kind);
}
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_with_saved_uses_saved);
    RUN_TEST(test_empty_without_saved_is_none);
    RUN_TEST(test_single_uses_discovered);
    RUN_TEST(test_multiple_is_ambiguous);
    RUN_TEST(test_multiple_with_saved_match_uses_it);
    RUN_TEST(test_multiple_with_saved_no_match_is_ambiguous);
    return UNITY_END();
}
