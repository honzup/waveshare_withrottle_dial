#include <unity.h>
#include "netprov_validation.h"
using namespace netprov;

void test_ipv4_accepts_valid() {
    TEST_ASSERT_TRUE(valid_ipv4("192.168.1.46"));
    TEST_ASSERT_TRUE(valid_ipv4("0.0.0.0"));
    TEST_ASSERT_TRUE(valid_ipv4("255.255.255.255"));
}
void test_ipv4_rejects_invalid() {
    TEST_ASSERT_FALSE(valid_ipv4("192.168.1"));
    TEST_ASSERT_FALSE(valid_ipv4("999.1.1.1"));
    TEST_ASSERT_FALSE(valid_ipv4("abc"));
    TEST_ASSERT_FALSE(valid_ipv4(""));
    TEST_ASSERT_FALSE(valid_ipv4("1.2.3.4.5"));
}
void test_port() {
    uint16_t p = 0;
    TEST_ASSERT_TRUE(valid_port("12090", p)); TEST_ASSERT_EQUAL_UINT16(12090, p);
    TEST_ASSERT_TRUE(valid_port("1", p));
    TEST_ASSERT_FALSE(valid_port("0", p));
    TEST_ASSERT_FALSE(valid_port("70000", p));
    TEST_ASSERT_FALSE(valid_port("x", p));
    TEST_ASSERT_FALSE(valid_port("", p));
}
void test_ssid() {
    TEST_ASSERT_TRUE(valid_ssid("MyNet"));
    TEST_ASSERT_FALSE(valid_ssid(""));
    TEST_ASSERT_FALSE(valid_ssid(std::string(33, 'a')));
}
void test_password() {
    TEST_ASSERT_TRUE(valid_password(""));            // open network
    TEST_ASSERT_TRUE(valid_password("password123"));
    TEST_ASSERT_FALSE(valid_password("short"));      // <8 and non-empty
    TEST_ASSERT_FALSE(valid_password(std::string(64, 'a')));
}
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ipv4_accepts_valid);
    RUN_TEST(test_ipv4_rejects_invalid);
    RUN_TEST(test_port);
    RUN_TEST(test_ssid);
    RUN_TEST(test_password);
    return UNITY_END();
}
