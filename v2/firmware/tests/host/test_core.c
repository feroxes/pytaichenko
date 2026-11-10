#include "unity.h"
#include "pt_core.h"

void setUp(void) {}
void tearDown(void) {}

static void test_version_comes_from_the_version_file(void)
{
    TEST_ASSERT_EQUAL_STRING(PT_VERSION, pt_version());
    TEST_ASSERT_EQUAL_STRING("2.0.0", pt_version());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_comes_from_the_version_file);
    return UNITY_END();
}
