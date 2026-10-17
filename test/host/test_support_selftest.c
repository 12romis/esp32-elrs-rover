// Самоперевірка макросів test_support.h: раннер ніколи не залишається з нулем тестів.
#include "test_support.h"

int main(void)
{
    CHECK(1 + 1 == 2);
    CHECK(sizeof(int) >= 2);
    return test_summary();
}
