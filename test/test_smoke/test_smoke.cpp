#include <unity.h>
void test_harness_works() { TEST_ASSERT_EQUAL_INT(4, 2 + 2); }
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_harness_works);
    return UNITY_END();
}
