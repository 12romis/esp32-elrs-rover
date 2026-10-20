// Хостові тести ioexp_core (spec AC-14, AC-15, AC-16): маска розрядів, незалежність каналів, збій, недоступність.
#include <stdint.h>

#include "ioexp_core.h"
#include "test_support.h"

#define B(n) ((uint8_t)(1u << (n)))
#define HEAD B(IOEXP_BIT_LIGHT_EN)
#define RED B(IOEXP_BIT_STOP_RED)
#define GREEN B(IOEXP_BIT_STOP_GREEN)

static ioexp_t started(void)
{
    ioexp_t e;
    ioexp_init(&e);
    ioexp_start_result(&e, true);
    return e;
}

static void test_mask_constant(void)
{
    CHECK(IOEXP_LIGHT_MASK == (HEAD | RED | GREEN));
    CHECK((IOEXP_LIGHT_MASK & B(IOEXP_BIT_XSHUT_U7)) == 0);
    CHECK((IOEXP_LIGHT_MASK & B(IOEXP_BIT_XSHUT_U8)) == 0);
    CHECK((IOEXP_LIGHT_MASK & B(IOEXP_BIT_XSHUT_U9)) == 0);
    CHECK((IOEXP_LIGHT_MASK & B(IOEXP_BIT_XSHUT_U10)) == 0);
    CHECK((IOEXP_LIGHT_MASK & 0x80) == 0);  // вільний розряд P7
}

static void test_start(void)
{
    ioexp_t e;
    ioexp_init(&e);
    CHECK(e.status == IOEXP_STATUS_UNKNOWN);
    CHECK(!ioexp_available(&e));
    uint8_t b = 0xAA;
    CHECK(!ioexp_prepare(&e, HEAD, &b));  // до старту писати не можна
    ioexp_start_result(&e, true);
    CHECK(e.status == IOEXP_STATUS_AVAILABLE);
    CHECK(ioexp_available(&e));
    CHECK(e.shadow == 0);
    CHECK(!e.bus_fault);
}

static void test_unavailable_stays(void)
{
    ioexp_t e;
    ioexp_init(&e);
    ioexp_start_result(&e, false);
    CHECK(e.status == IOEXP_STATUS_UNAVAILABLE);
    CHECK(!ioexp_available(&e));
    uint8_t b = 0;
    CHECK(!ioexp_prepare(&e, HEAD, &b));
    CHECK(!ioexp_needs_write(&e, HEAD));
    // повторний «успіх» після відмови на старті статусу не міняє: недоступний до перезапуску
    ioexp_start_result(&e, true);
    CHECK(e.status == IOEXP_STATUS_UNAVAILABLE);
}

static void test_independent_channels(void)
{
    ioexp_t e = started();
    uint8_t b = 0;
    CHECK(ioexp_prepare(&e, HEAD, &b) && b == HEAD);
    CHECK(ioexp_commit(&e, b, true) == IOEXP_EVT_NONE);
    CHECK(ioexp_bit(&e, IOEXP_BIT_LIGHT_EN) && !ioexp_bit(&e, IOEXP_BIT_STOP_RED) && !ioexp_bit(&e, IOEXP_BIT_STOP_GREEN));
    CHECK(ioexp_prepare(&e, (uint8_t)(HEAD | RED), &b) && b == (HEAD | RED));
    ioexp_commit(&e, b, true);
    CHECK(ioexp_prepare(&e, (uint8_t)(HEAD | RED | GREEN), &b) && b == (HEAD | RED | GREEN));
    ioexp_commit(&e, b, true);
    CHECK(ioexp_prepare(&e, (uint8_t)(RED | GREEN), &b) && b == (RED | GREEN));  // фари вимкнено окремо
    ioexp_commit(&e, b, true);
    CHECK(!ioexp_bit(&e, IOEXP_BIT_LIGHT_EN) && ioexp_bit(&e, IOEXP_BIT_STOP_RED) && ioexp_bit(&e, IOEXP_BIT_STOP_GREEN));
}

static void test_mask_for_any_input(void)
{
    ioexp_t e = started();
    for (int v = 0; v < 256; v++) {
        uint8_t b = 0xFF;
        CHECK(ioexp_prepare(&e, (uint8_t)v, &b));
        CHECK((b & (uint8_t)~IOEXP_LIGHT_MASK) == 0);  // розряди далекомірів і вільний завжди 0
        CHECK(b == (v & IOEXP_LIGHT_MASK));
    }
}

static void test_needs_write(void)
{
    ioexp_t e = started();
    CHECK(!ioexp_needs_write(&e, 0));          // копія 0, бажане 0
    CHECK(ioexp_needs_write(&e, HEAD));        // відрізняється
    CHECK(!ioexp_needs_write(&e, 0x01));       // розряд скидання далекоміра під маскою не рахується
    ioexp_commit(&e, HEAD, true);
    CHECK(!ioexp_needs_write(&e, HEAD));
}

static void test_failed_write_keeps_shadow(void)
{
    ioexp_t e = started();
    ioexp_commit(&e, HEAD, true);
    CHECK(ioexp_commit(&e, (uint8_t)(HEAD | RED), false) == IOEXP_EVT_FAULT);
    CHECK(e.shadow == HEAD);          // копія не змінилась
    CHECK(e.bus_fault);
    CHECK(ioexp_needs_write(&e, HEAD));  // повтор навіть без розбіжності
    CHECK(ioexp_commit(&e, (uint8_t)(HEAD | RED), false) == IOEXP_EVT_NONE);  // друга невдача — без нового повідомлення
    CHECK(ioexp_commit(&e, (uint8_t)(HEAD | RED), true) == IOEXP_EVT_RECOVERED);
    CHECK(e.shadow == (HEAD | RED));
    CHECK(!e.bus_fault);
    CHECK(!ioexp_needs_write(&e, (uint8_t)(HEAD | RED)));
}

// ───────── phase6-tof T4: розряди скидання далекомірів (ADR-0002) ─────────
#define X(n) ((uint8_t)(1u << (n)))
#define XS_ALL (X(IOEXP_BIT_XSHUT_U7) | X(IOEXP_BIT_XSHUT_U8) | X(IOEXP_BIT_XSHUT_U9) | X(IOEXP_BIT_XSHUT_U10))

static void test_xshut_mask_constant(void)
{
    CHECK(IOEXP_XSHUT_MASK == XS_ALL);
    CHECK((IOEXP_XSHUT_MASK & IOEXP_LIGHT_MASK) == 0);   // світло й скидання не перетинаються
    CHECK((IOEXP_XSHUT_MASK & 0x80) == 0);
}

static void test_xshut_preserved_by_light_write(void)
{
    ioexp_t e = started();
    uint8_t b = 0;
    CHECK(ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U7, true));
    CHECK(ioexp_prepare(&e, 0, &b) && b == X(IOEXP_BIT_XSHUT_U7));
    CHECK(ioexp_needs_write(&e, 0));                       // бажаний розряд відрізняється від підтвердженого
    ioexp_commit(&e, b, true);
    CHECK(!ioexp_needs_write(&e, 0));
    CHECK(ioexp_prepare(&e, (uint8_t)(HEAD | RED), &b));   // запис світла зберігає розряд скидання
    CHECK(b == (X(IOEXP_BIT_XSHUT_U7) | HEAD | RED));
    ioexp_commit(&e, b, true);
    CHECK(ioexp_prepare(&e, 0, &b) && b == X(IOEXP_BIT_XSHUT_U7));   // вимкнення світла теж
}

static void test_xshut_preserved_by_refresh(void)
{
    ioexp_t e = started();
    uint8_t b = 0;
    ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U8, true);
    ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U10, true);
    ioexp_prepare(&e, HEAD, &b);
    ioexp_commit(&e, b, true);
    // щосекундний перезапис того самого бажаного світла дає той самий байт
    uint8_t b2 = 0;
    CHECK(ioexp_prepare(&e, HEAD, &b2) && b2 == b);
    CHECK(b2 == (X(IOEXP_BIT_XSHUT_U8) | X(IOEXP_BIT_XSHUT_U10) | HEAD));
}

static void test_xshut_set_and_clear_and_foreign_bits(void)
{
    ioexp_t e = started();
    uint8_t b = 0;
    CHECK(!ioexp_xshut_set(&e, IOEXP_BIT_LIGHT_EN, true));     // біт світла
    CHECK(!ioexp_xshut_set(&e, IOEXP_BIT_STOP_RED, true));
    CHECK(!ioexp_xshut_set(&e, 7, true));                      // вільний розряд
    CHECK(!ioexp_xshut_set(&e, 8, true));
    CHECK(!ioexp_xshut_set(&e, -1, true));
    CHECK(ioexp_prepare(&e, 0, &b) && b == 0);
    CHECK(ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U9, true));
    CHECK(ioexp_prepare(&e, 0, &b) && b == X(IOEXP_BIT_XSHUT_U9));
    CHECK(ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U9, false));
    CHECK(ioexp_prepare(&e, 0, &b) && b == 0);
    for (int v = 0; v < 256; v++) {   // для будь-якого бажаного світла чужі розряди нуль
        ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U7, true);
        CHECK(ioexp_prepare(&e, (uint8_t)v, &b));
        CHECK((b & (uint8_t)~(IOEXP_LIGHT_MASK | IOEXP_XSHUT_MASK)) == 0);
        CHECK((b & IOEXP_XSHUT_MASK) == X(IOEXP_BIT_XSHUT_U7));
    }
}

static void test_first_write_zero_has_no_xshut(void)
{
    ioexp_t e;
    ioexp_init(&e);
    ioexp_start_result(&e, true);        // перший запис нуля підтверджено
    CHECK(e.shadow == 0);
    uint8_t b = 0xFF;
    CHECK(ioexp_prepare(&e, 0, &b) && b == 0);   // розряди скидання бажані 0 до першого ioexp_xshut_set
}

static void test_xshut_revert_after_failed_write(void)
{
    ioexp_t e = started();
    uint8_t b = 0;
    ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U7, true);
    ioexp_prepare(&e, 0, &b);
    ioexp_commit(&e, b, false);          // запис не підтверджено
    CHECK(e.shadow == 0);
    ioexp_xshut_revert(&e);              // бажані розряди назад до підтверджених
    CHECK(ioexp_prepare(&e, 0, &b) && b == 0);
    // підтверджений розряд лишається після невдалої зміни іншого
    ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U8, true);
    ioexp_prepare(&e, 0, &b);
    ioexp_commit(&e, b, true);
    ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U9, true);
    ioexp_prepare(&e, 0, &b);
    ioexp_commit(&e, b, false);
    ioexp_xshut_revert(&e);
    CHECK(ioexp_prepare(&e, 0, &b) && b == X(IOEXP_BIT_XSHUT_U8));
}

static void test_xshut_unavailable(void)
{
    ioexp_t e;
    ioexp_init(&e);
    ioexp_start_result(&e, false);
    CHECK(!ioexp_xshut_set(&e, IOEXP_BIT_XSHUT_U7, true));      // недоступний: розряд не змінюється
    uint8_t b = 0;
    CHECK(!ioexp_prepare(&e, 0, &b));
}

int main(void)
{
    test_mask_constant();
    test_start();
    test_unavailable_stays();
    test_independent_channels();
    test_mask_for_any_input();
    test_needs_write();
    test_failed_write_keeps_shadow();
    test_xshut_mask_constant();
    test_xshut_preserved_by_light_write();
    test_xshut_preserved_by_refresh();
    test_xshut_set_and_clear_and_foreign_bits();
    test_first_write_zero_has_no_xshut();
    test_xshut_revert_after_failed_write();
    test_xshut_unavailable();
    return test_summary();
}
