// Хостові тести ui_core (spec AC-04…AC-23): дозволи, задні ліхтарі, індикатор, beep, сигнали переходів, FAILSAFE.
#include <stdint.h>

#include "test_support.h"
#include "ui_core.h"

#define HZ 1000u  // довільна частота автосигналів у діапазоні зумера (значення задасть Розробник за OQ-2)

static const app_state_t ALL[APP_STATE_COUNT] = {APP_STATE_BOOT, APP_STATE_SELFTEST, APP_STATE_DISARMED, APP_STATE_ARMED,
                                                 APP_STATE_OBSTACLE_HOLD, APP_STATE_FAILSAFE, APP_STATE_FAULT};

static ui_in_t in_of(uint32_t t, app_state_t s, int l, int r, bool ok)
{
    ui_in_t i = {.now_ms = t, .state = s, .left_permille = (int16_t)l, .right_permille = (int16_t)r, .expander_ok = ok};
    return i;
}

static ui_out_t tick(ui_core_t *u, uint32_t t, app_state_t s, int l, int r, bool ok)
{
    ui_in_t i = in_of(t, s, l, r, ok);
    ui_out_t o;
    ui_core_tick(u, &i, &o);
    return o;
}

// ---------- дозволи (AC-06, AC-07): 7 станів × (light, beep) і 7 × 3 канали ----------
static void test_permissions(void)
{
    for (int i = 0; i < APP_STATE_COUNT; i++) {
        app_state_t s = ALL[i];
        bool ok = s == APP_STATE_DISARMED || s == APP_STATE_ARMED;
        CHECK(ui_core_state_allows(s) == ok);  // beep і light за станом
    }
    int pairs = 0;
    for (int i = 0; i < APP_STATE_COUNT; i++) {
        app_state_t s = ALL[i];
        for (int c = 0; c < UI_CH_COUNT; c++) {
            bool expect = (s == APP_STATE_DISARMED) || (s == APP_STATE_ARMED && c == UI_CH_HEAD);
            CHECK(ui_core_channel_allows(s, (ui_channel_t)c) == expect);
            pairs++;
        }
    }
    CHECK(pairs == 21);
    printf("ПОКРИТТЯ пар канал × стан (light): %d з 21\n", pairs);
    CHECK(ui_core_rear_mode(APP_STATE_ARMED) == UI_REAR_AUTO);
    CHECK(ui_core_rear_mode(APP_STATE_FAILSAFE) == UI_REAR_AUTO);
    CHECK(ui_core_rear_mode(APP_STATE_OBSTACLE_HOLD) == UI_REAR_AUTO);   // phase6-tof ADR-0004
    CHECK(ui_core_rear_mode(APP_STATE_DISARMED) == UI_REAR_MANUAL);
}

// ---------- ручний light: порядок перевірок і незалежність каналів ----------
static void test_manual_light(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    CHECK(ui_core_light(&u, UI_CH_HEAD, true, APP_STATE_FAILSAFE, true) == UI_DENY_STATE);
    CHECK(ui_core_light(&u, UI_CH_RED, true, APP_STATE_ARMED, true) == UI_DENY_MOTION);
    CHECK(ui_core_light(&u, UI_CH_GREEN, true, APP_STATE_ARMED, false) == UI_DENY_MOTION);  // канал перед доступністю
    CHECK(ui_core_light(&u, UI_CH_HEAD, true, APP_STATE_DISARMED, false) == UI_DENY_EXPANDER);
    CHECK(ui_core_light(&u, UI_CH_HEAD, true, APP_STATE_BOOT, false) == UI_DENY_STATE);     // стан перед доступністю
    CHECK(!u.manual[UI_CH_HEAD]);  // відмови нічого не змінили
    CHECK(ui_core_light(&u, UI_CH_HEAD, true, APP_STATE_DISARMED, true) == UI_OK);
    CHECK(ui_core_light(&u, UI_CH_RED, true, APP_STATE_DISARMED, true) == UI_OK);
    CHECK(ui_core_light(&u, UI_CH_GREEN, true, APP_STATE_DISARMED, true) == UI_OK);
    CHECK(u.manual[UI_CH_HEAD] && u.manual[UI_CH_RED] && u.manual[UI_CH_GREEN]);  // разом червоний і зелений
    CHECK(ui_core_light(&u, UI_CH_HEAD, false, APP_STATE_ARMED, true) == UI_OK);   // фари в ARMED дозволені
    CHECK(!u.manual[UI_CH_HEAD] && u.manual[UI_CH_RED]);
    ui_core_rollback(&u, UI_CH_HEAD, true);  // збій запису: відкат
    CHECK(u.manual[UI_CH_HEAD]);
}

// ---------- задні ліхтарі й фари: DISARMED червоні, ARMED жовті чи зелені (AC-17, AC-18, AC-27) ----------
static void test_rear_by_motion(void)
{
    ui_core_t u;
    ui_core_init(&u, 0);  // автосигнали вимкнені, перевіряємо лише світло
    ui_out_t o = tick(&u, 0, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.red && !o.green && !o.head);              // DISARMED: задні червоні за замовчуванням, фари вимкнені
    o = tick(&u, 100, APP_STATE_ARMED, 0, 0, true);   // одразу після озброєння
    CHECK(o.red && o.green);                          // жовтий
    CHECK(o.head);                                    // фари вмикаються самі
    o = tick(&u, 200, APP_STATE_ARMED, 300, 300, true);   // вперед
    CHECK(o.red && o.green);
    o = tick(&u, 300, APP_STATE_ARMED, -300, -300, true);  // назад: лише зелений
    CHECK(!o.red && o.green);
    o = tick(&u, 400, APP_STATE_ARMED, 400, -400, true);   // поворот на місці: сума 0 — жовтий
    CHECK(o.red && o.green);
    o = tick(&u, 500, APP_STATE_ARMED, 750, 230, true);    // насичення, сума додатна
    CHECK(o.red && o.green);
    o = tick(&u, 600, APP_STATE_ARMED, -100, -101, true);  // сума від'ємна
    CHECK(!o.red && o.green);
    // інваріант: у ARMED зелений світить завжди (задні ніколи не вимкнені і ніколи не лише червоні)
    for (int l = -750; l <= 750; l += 125)
        for (int r = -750; r <= 750; r += 125) {
            o = tick(&u, 700, APP_STATE_ARMED, l, r, true);
            CHECK(o.green && (o.red == ((l + r) >= 0)));
        }
}

static void test_head_and_leave_armed(void)
{
    ui_core_t u;
    ui_core_init(&u, 0);
    ui_out_t o = tick(&u, 0, APP_STATE_ARMED, 0, 0, false);  // деградація: розширювач недоступний
    CHECK(!o.red && !o.green);                                 // ліхтарі не вмикаються, це не порушення
    CHECK(o.head);                                             // бажаний стан фар зафіксований
    o = tick(&u, 50, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.head && o.red && !o.green);                       // DISARMED: фари гаснуть, задні червоні
    // ручне світло в DISARMED
    CHECK(ui_core_light(&u, UI_CH_HEAD, true, APP_STATE_DISARMED, true) == UI_OK);
    CHECK(ui_core_light(&u, UI_CH_RED, false, APP_STATE_DISARMED, true) == UI_OK);
    CHECK(ui_core_light(&u, UI_CH_GREEN, true, APP_STATE_DISARMED, true) == UI_OK);
    o = tick(&u, 100, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.head && !o.red && o.green);                        // ручні значення тримаються
    // вхід в ARMED: фари вмикаються самі (вже ввімкнені), задні керує рух
    o = tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.head && o.red && o.green);
    // light head off в ARMED діє до виходу
    CHECK(ui_core_light(&u, UI_CH_HEAD, false, APP_STATE_ARMED, true) == UI_OK);
    o = tick(&u, 300, APP_STATE_ARMED, 300, 300, true);
    CHECK(!o.head && o.red && o.green);
    o = tick(&u, 400, APP_STATE_ARMED, 300, 300, true);
    CHECK(!o.head);                                            // фари самі не повертаються
    // вихід з ARMED (роззброєння): фари гаснуть, задні червоні
    ui_core_light(&u, UI_CH_HEAD, true, APP_STATE_ARMED, true);
    o = tick(&u, 500, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.head && o.red && !o.green);
    o = tick(&u, 600, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.head && o.red && !o.green);                       // стан тримається
    // знову ARMED: фари вмикаються самі
    o = tick(&u, 700, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.head);
    // FAILSAFE: фари гаснуть, задні блимають червоним
    o = tick(&u, 800, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.head && o.red && !o.green);
}

// ---------- індикатор плати (AC-20): сім станів ----------
static void test_indicator(void)
{
    for (int i = 0; i < APP_STATE_COUNT; i++) {
        ui_core_t u;
        ui_core_init(&u, 0);
        ui_out_t o = tick(&u, 0, ALL[i], 0, 0, true);
        CHECK((o.indicator == UI_IND_RED) == (ALL[i] == APP_STATE_FAILSAFE));
        ui_core_t v;
        ui_core_init(&v, 0);
        o = tick(&v, 0, ALL[i], 0, 0, false);  // без розширювача індикатор такий самий
        CHECK((o.indicator == UI_IND_RED) == (ALL[i] == APP_STATE_FAILSAFE));
    }
}


// ---------- ручний beep (AC-08, AC-09) ----------
static void settle(ui_core_t *u, uint32_t t0, app_state_t st)
{
    tick(u, t0, st, 0, 0, true);
    tick(u, t0 + 100, st, 0, 0, true);  // стартовий сигнал переходу закінчився
}

static void test_manual_beep(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    bool q = true;
    CHECK(ui_core_beep(&u, 1000, 300, 200, APP_STATE_DISARMED, &q) == UI_OK);
    CHECK(!q);
    CHECK(u.out.buzzer_on && u.out.buzzer_hz == 1000);  // звучить одразу, без очікування такту
    ui_out_t o = tick(&u, 250, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == 1000);
    CHECK(ui_core_remaining_ms(&u, 250) == 250);
    o = tick(&u, 450, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.buzzer_on);
    o = tick(&u, 500, APP_STATE_DISARMED, 0, 0, true);  // рівно 300 мс від початку: замовкає
    CHECK(!o.buzzer_on);
    CHECK(ui_core_remaining_ms(&u, 500) == 0);
}

static void test_beep_replace(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    ui_core_beep(&u, 1000, 2000, 200, APP_STATE_DISARMED, NULL);
    ui_core_beep(&u, 2500, 300, 400, APP_STATE_DISARMED, NULL);  // нова команда замінює
    CHECK(u.out.buzzer_on && u.out.buzzer_hz == 2500);
    ui_out_t o = tick(&u, 650, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == 2500);
    CHECK(ui_core_remaining_ms(&u, 650) == 50);              // тривалість — від нової команди
    o = tick(&u, 700, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.buzzer_on);                                     // старий звук не повертається
}

static void test_beep_range_and_state(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    ui_core_beep(&u, 1000, 2000, 200, APP_STATE_DISARMED, NULL);
    CHECK(ui_core_beep(&u, 199, 100, 300, APP_STATE_DISARMED, NULL) == UI_DENY_RANGE);
    CHECK(ui_core_beep(&u, 5001, 100, 300, APP_STATE_DISARMED, NULL) == UI_DENY_RANGE);
    CHECK(ui_core_beep(&u, 1000, 0, 300, APP_STATE_DISARMED, NULL) == UI_DENY_RANGE);
    CHECK(ui_core_beep(&u, 1000, 2001, 300, APP_STATE_DISARMED, NULL) == UI_DENY_RANGE);
    CHECK(u.out.buzzer_on && u.out.buzzer_hz == 1000);  // відмови не змінили звучання, що триває
    CHECK(ui_core_beep(&u, 100, 100, 300, APP_STATE_FAILSAFE, NULL) == UI_DENY_STATE);  // стан перед діапазоном
    CHECK(ui_core_beep(&u, 200, 1, 300, APP_STATE_ARMED, NULL) == UI_OK);               // межі входять
    CHECK(ui_core_beep(&u, 5000, 2000, 300, APP_STATE_ARMED, NULL) == UI_OK);
    for (int i = 0; i < APP_STATE_COUNT; i++) {
        ui_core_t v;
        ui_core_init(&v, HZ);
        ui_result_t r = ui_core_beep(&v, 1000, 100, 0, ALL[i], NULL);
        CHECK((r == UI_OK) == ui_core_state_allows(ALL[i]));
    }
}

// ---------- сигнали переходів (AC-21, AC-23) ----------
static void test_transition_signals(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    ui_out_t o = tick(&u, 0, APP_STATE_BOOT, 0, 0, true);
    CHECK(!o.buzzer_on);                         // BOOT, SELFTEST — без сигналу
    o = tick(&u, 10, APP_STATE_SELFTEST, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 20, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);     // один сигнал на старті
    o = tick(&u, 119, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.buzzer_on);
    o = tick(&u, 120, APP_STATE_DISARMED, 0, 0, true);  // 100 мс
    CHECK(!o.buzzer_on);
    o = tick(&u, 220, APP_STATE_DISARMED, 0, 0, true);  // той самий стан — без сигналу
    CHECK(!o.buzzer_on);
    o = tick(&u, 320, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.buzzer_on);                          // DISARMED → ARMED
    o = tick(&u, 420, APP_STATE_ARMED, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 520, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.buzzer_on);                          // ARMED → DISARMED (зокрема автоматичне роззброєння)
    // повільний хост: перший спостережений стан одразу DISARMED — теж один сигнал
    ui_core_t v;
    ui_core_init(&v, HZ);
    o = tick(&v, 0, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.buzzer_on);
    // інші стани сигналу не дають (OBSTACLE_HOLD як вхід у рух із DISARMED — окремо, test_hold_*)
    ui_core_t w;
    ui_core_init(&w, HZ);
    settle(&w, 0, APP_STATE_DISARMED);
    o = tick(&w, 300, APP_STATE_FAULT, 0, 0, true);
    CHECK(!o.buzzer_on);
}

static void test_signal_replaces_manual_and_slot(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_ARMED);
    ui_core_beep(&u, 2000, 2000, 200, APP_STATE_ARMED, NULL);      // довгий ручний звук
    ui_out_t o = tick(&u, 300, APP_STATE_DISARMED, 0, 0, true);    // автороззброєння
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);                       // сигнал замінив ручний
    o = tick(&u, 400, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // ручний не повертається
    // ручний beep під час сигналу переходу лягає в слот
    bool q = false;
    o = tick(&u, 1000, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);
    CHECK(ui_core_beep(&u, 1500, 300, 1010, APP_STATE_ARMED, &q) == UI_OK);
    CHECK(q);
    CHECK(u.out.buzzer_hz == HZ);                                  // сигнал не перервано
    o = tick(&u, 1050, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);
    ui_core_beep(&u, 1800, 300, 1060, APP_STATE_ARMED, &q);         // наступна команда перезаписує слот
    CHECK(q);
    o = tick(&u, 1100, APP_STATE_ARMED, 0, 0, true);               // сигнал закінчився, слот звучить
    CHECK(o.buzzer_on && o.buzzer_hz == 1800);
    o = tick(&u, 1300, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.buzzer_on);
    o = tick(&u, 1400, APP_STATE_ARMED, 0, 0, true);
    CHECK(!o.buzzer_on);
}

static void test_slot_cleared_on_failsafe(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    bool q = false;
    tick(&u, 1000, APP_STATE_ARMED, 0, 0, true);                       // сигнал входу в ARMED
    CHECK(ui_core_beep(&u, 1500, 2000, 1010, APP_STATE_ARMED, &q) == UI_OK && q);  // довгий ручний beep ліг у слот
    ui_out_t o = tick(&u, 1020, APP_STATE_FAILSAFE, 0, 0, true);       // відсічка в той самий момент
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);                            // сигнал FAILSAFE
    o = tick(&u, 1120, APP_STATE_FAILSAFE, 0, 0, true);                // сигнал закінчився
    CHECK(!o.buzzer_on);                                                // слот не відтворюється у FAILSAFE (AC-06, AC-23)
    o = tick(&u, 1500, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);
}

static void test_no_signal_without_calibration(void)
{
    ui_core_t u;
    ui_core_init(&u, 0);  // частота автосигналів не задана (OQ-2): автосигналів немає
    ui_out_t o = tick(&u, 0, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 100, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 2100, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);
    CHECK(o.indicator == UI_IND_RED);  // індикація працює без частоти
    // ручний beep працює і без калібрування
    ui_core_t v;
    ui_core_init(&v, 0);
    CHECK(ui_core_beep(&v, 1000, 100, 0, APP_STATE_DISARMED, NULL) == UI_OK);
    CHECK(v.out.buzzer_on);
}

// ---------- FAILSAFE (AC-19, AC-22, AC-23) ----------
static void test_failsafe_timeline(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_ARMED);
    ui_out_t o = tick(&u, 1000, APP_STATE_FAILSAFE, 0, 0, true);  // вхід
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);                       // сигнал переходу
    CHECK(o.indicator == UI_IND_RED);
    CHECK(o.red && !o.green);                                      // блимання починається зі «світить»
    o = tick(&u, 1100, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 1499, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(o.red);
    o = tick(&u, 1500, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.red && !o.green);                                     // 500 мс вимкнено
    o = tick(&u, 2000, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(o.red);
    CHECK(!o.green);
    o = tick(&u, 2999, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // перший періодичний — на 2000 мс від входу
    o = tick(&u, 3000, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);
    o = tick(&u, 3199, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(o.buzzer_on);
    o = tick(&u, 3200, APP_STATE_FAILSAFE, 0, 0, true);            // 200 мс
    CHECK(!o.buzzer_on);
    o = tick(&u, 5000, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(o.buzzer_on);
    o = tick(&u, 5200, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 7000, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(o.buzzer_on);                                            // і далі кожні 2000 мс
    CHECK(ui_core_beep(&u, 1000, 100, 7100, APP_STATE_FAILSAFE, NULL) == UI_DENY_STATE);
}

static void test_failsafe_without_expander(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_ARMED);
    ui_out_t o = tick(&u, 1000, APP_STATE_FAILSAFE, 0, 0, false);
    CHECK(!o.red && !o.green);            // блимання немає
    CHECK(o.indicator == UI_IND_RED);     // індикатор і звук працюють
    CHECK(o.buzzer_on);
    o = tick(&u, 3000, APP_STATE_FAILSAFE, 0, 0, false);
    CHECK(o.buzzer_on);
}

static void test_silence_outside_failsafe(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_ARMED);
    int sounded = 0;
    for (uint32_t t = 200; t < 20000; t += 100) {
        ui_out_t o = tick(&u, t, APP_STATE_ARMED, 100, 100, true);
        sounded += o.buzzer_on;
    }
    CHECK(sounded == 0);  // тиша означає відсутність аварії
    sounded = 0;
    for (uint32_t t = 20000; t < 30000; t += 100) {
        ui_out_t o = tick(&u, t, APP_STATE_DISARMED, 0, 0, true);
        if (t > 20100) sounded += o.buzzer_on;
    }
    CHECK(sounded == 0);
}

// ---------- Вихід із FAILSAFE у DISARMED (phase3-crsf, AC-08): раніше цього переходу не існувало ----------
static void test_failsafe_exit_to_disarmed(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_ARMED);
    tick(&u, 1000, APP_STATE_FAILSAFE, 0, 0, true);                // вхід: сигнал переходу
    ui_out_t o = tick(&u, 3000, APP_STATE_FAILSAFE, 0, 0, true);   // періодичний писк іде
    CHECK(o.buzzer_on);
    // Вихід посеред звучання періодичного сигналу: звучання замінює сигнал переходу в DISARMED.
    o = tick(&u, 3100, APP_STATE_DISARMED, 0, 0, true);
    CHECK(o.indicator == UI_IND_OFF);                              // червоний індикатор погас
    CHECK(o.red && !o.green);                                      // задні ліхтарі червоні постійно (DISARMED за замовчуванням)
    CHECK(!o.head);
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);                       // один сигнал переходу в DISARMED
    o = tick(&u, 3200, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // сигнал 100 мс закінчився
    // Блимання припинилося: ліхтарі світять постійно, а не мерехтять із періодом FAILSAFE.
    for (uint32_t t = 3300; t <= 5300; t += 100) {
        o = tick(&u, t, APP_STATE_DISARMED, 0, 0, true);
        CHECK(o.red && !o.green);
        CHECK(!o.buzzer_on);                                       // періодичного писку немає
        CHECK(o.indicator == UI_IND_OFF);
    }
}

static void test_failsafe_reentry_restarts_timeline(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_ARMED);
    tick(&u, 1000, APP_STATE_FAILSAFE, 0, 0, true);
    tick(&u, 1600, APP_STATE_FAILSAFE, 0, 0, true);                // у фазі «не світить»
    tick(&u, 2000, APP_STATE_DISARMED, 0, 0, true);                // вихід
    settle(&u, 2500, APP_STATE_ARMED);
    ui_out_t o = tick(&u, 4000, APP_STATE_FAILSAFE, 0, 0, true);   // новий вхід
    CHECK(o.red && !o.green);                                      // блимання знову починається зі «світить»
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);                       // новий сигнал переходу
    o = tick(&u, 5999, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // перший періодичний — через 2000 мс від нового входу
    o = tick(&u, 6000, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(o.buzzer_on);
}

// ---------- phase6-tof (ADR-0004): OBSTACLE_HOLD — стан руху ----------
static void test_hold_rear_auto_and_double_beep_transitions(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    ui_out_t o = tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.head && o.buzzer_on);                       // озброєння: фари й одинарний сигнал, як у фазі 2
    o = tick(&u, 300, APP_STATE_ARMED, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(o.head);                                      // фари не змінились
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);            // phase7-telemetry A5: вхід в утримання — подвійний біп, перший звук
    CHECK(o.indicator == UI_IND_YELLOW);                // A8: жовтий увесь час утримання
    CHECK(o.red && o.green);                            // стоїть: жовтий
    o = tick(&u, 500, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.buzzer_on);                                // пауза 100 мс
    o = tick(&u, 600, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);            // другий звук
    o = tick(&u, 700, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.buzzer_on && o.indicator == UI_IND_YELLOW);
    o = tick(&u, 800, APP_STATE_OBSTACLE_HOLD, -300, -300, true);
    CHECK(!o.red && o.green);                           // назад: лише зелений, як в ARMED
    CHECK(!o.buzzer_on);
    o = tick(&u, 900, APP_STATE_ARMED, -300, -300, true);
    CHECK(o.head && o.buzzer_on);                       // вихід в ARMED — одинарний сигнал 200 мс (коміт bc4aedd); фари не чіпає
    CHECK(o.indicator == UI_IND_OFF);                   // індикатор гасне
    CHECK(!o.red && o.green);
    o = tick(&u, 1000, APP_STATE_ARMED, -300, -300, true);
    CHECK(o.buzzer_on);
    o = tick(&u, 1100, APP_STATE_ARMED, -300, -300, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 1200, APP_STATE_ARMED, -300, -300, true);
    CHECK(!o.buzzer_on);
    CHECK(ui_core_remaining_ms(&u, 1200) == 0);
}

static void test_hold_does_not_force_headlights(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    CHECK(ui_core_light(&u, UI_CH_HEAD, false, APP_STATE_ARMED, true) == UI_OK);   // Розробник вимкнув фари
    ui_out_t o = tick(&u, 300, APP_STATE_ARMED, 0, 0, true);
    CHECK(!o.head);
    o = tick(&u, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.head);
    o = tick(&u, 800, APP_STATE_ARMED, 0, 0, true);
    CHECK(!o.head && o.buzzer_on);                      // повернення в ARMED не вмикає фари знову (ADR-0004), але біпає
}

static void test_disarmed_to_hold_double_beep_replaces_arming(void)
{
    // ui опитує раз на 100 мс: озброєння біля вже зайнятого сектору виглядає як DISARMED → OBSTACLE_HOLD
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    ui_out_t o = tick(&u, 200, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(o.head);                                      // фари вмикаються як при вході в рух
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);
    CHECK(o.red && o.green && o.indicator == UI_IND_YELLOW);
    o = tick(&u, 300, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.buzzer_on);                                // пауза: це подвійний біп, а не одинарний сигнал озброєння
    o = tick(&u, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(o.buzzer_on);                                 // другий звук
}

static void test_hold_exits(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    tick(&u, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    ui_out_t o = tick(&u, 600, APP_STATE_DISARMED, 0, 0, true);   // роззброєння з утримання
    CHECK(!o.head && o.buzzer_on);                                 // одинарний сигнал DISARMED
    CHECK(o.red && !o.green);                                      // ручний стан DISARMED: червоний
    CHECK(o.indicator == UI_IND_OFF);
    o = tick(&u, 700, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 800, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // подвійного біпа на виході в DISARMED немає
    ui_core_t w;
    ui_core_init(&w, HZ);
    settle(&w, 0, APP_STATE_DISARMED);
    tick(&w, 200, APP_STATE_ARMED, 0, 0, true);
    tick(&w, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    o = tick(&w, 500, APP_STATE_FAILSAFE, 0, 0, true);             // FAILSAFE з утримання, у паузі подвійного біпа
    CHECK(!o.head && o.buzzer_on);                                 // звучить сигнал FAILSAFE: біп справді перервано
    CHECK(o.indicator == UI_IND_RED);                              // червоний, не жовтий
    o = tick(&w, 600, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // другий звук подвійного біпа не доспівав
    o = tick(&w, 800, APP_STATE_FAILSAFE, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // лише сигнали FAILSAFE: наступний писк через 2000 мс
}

// Такт ui зсунуто на 1 мс (задачу sensors вищого пріоритету): подвійний біп лишається 100/100/100 мс.
static void test_double_beep_tolerates_tick_jitter(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    ui_out_t o = tick(&u, 401, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(o.buzzer_on);
    o = tick(&u, 500, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 600, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(o.buzzer_on);
    o = tick(&u, 700, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.buzzer_on);
    o = tick(&u, 800, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.buzzer_on);
    // зсув в інший бік: 390 (раніше сітки) і 405
    ui_core_t v;
    ui_core_init(&v, HZ);
    settle(&v, 0, APP_STATE_DISARMED);
    tick(&v, 200, APP_STATE_ARMED, 0, 0, true);
    tick(&v, 390, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!tick(&v, 495, APP_STATE_OBSTACLE_HOLD, 0, 0, true).buzzer_on);
    CHECK(tick(&v, 590, APP_STATE_OBSTACLE_HOLD, 0, 0, true).buzzer_on);
    CHECK(!tick(&v, 690, APP_STATE_OBSTACLE_HOLD, 0, 0, true).buzzer_on);
}

static void test_double_beep_no_overlap(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    tick(&u, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);            // подвійний біп 400…700
    tick(&u, 500, APP_STATE_ARMED, 0, 0, true);                    // вихід під час біпа — нова подія не стартує
    ui_out_t o = tick(&u, 600, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(o.buzzer_on);                                            // другий звук першого біпа не обірвано
    o = tick(&u, 700, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // біп скінчився, нового не було
    o = tick(&u, 800, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(o.buzzer_on);                                           // повторний вхід в утримання почав новий подвійний біп (коміт bc4aedd)
}

static void test_double_beep_replaced_by_disarm_signal(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    tick(&u, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    ui_out_t o = tick(&u, 500, APP_STATE_DISARMED, 0, 0, true);    // у паузі подвійного біпа
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);                       // сигнал DISARMED замінив біп
    o = tick(&u, 600, APP_STATE_DISARMED, 0, 0, true);
    CHECK(!o.buzzer_on);                                           // решта подвійного біпа не звучить
}

static void test_double_beep_manual_beep_waits_in_slot(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    tick(&u, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    tick(&u, 800, APP_STATE_ARMED, 0, 0, true);                    // вихідний одинарний сигнал 800…1000
    bool queued = false;
    CHECK(ui_core_beep(&u, 2000, 500, 850, APP_STATE_ARMED, &queued) == UI_OK && queued);   // чекає в слоті
    ui_out_t o = tick(&u, 900, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == HZ);                       // біп не вкорочено
    o = tick(&u, 1000, APP_STATE_ARMED, 0, 0, true);
    CHECK(o.buzzer_on && o.buzzer_hz == 2000);                     // після біпа звучить ручний
}

static void test_hold_indicator_and_no_calibration(void)
{
    ui_core_t u;
    ui_core_init(&u, 0);                                           // частота автосигналів не задана
    settle(&u, 0, APP_STATE_DISARMED);
    tick(&u, 200, APP_STATE_ARMED, 0, 0, true);
    ui_out_t o = tick(&u, 400, APP_STATE_OBSTACLE_HOLD, 0, 0, true);
    CHECK(!o.buzzer_on && o.indicator == UI_IND_YELLOW);           // без частоти звуку немає, колір є
    for (int i = 0; i < APP_STATE_COUNT; i++) {
        ui_core_t v;
        ui_core_init(&v, HZ);
        o = tick(&v, 0, ALL[i], 0, 0, true);
        ui_indicator_t want = ALL[i] == APP_STATE_FAILSAFE ? UI_IND_RED : (ALL[i] == APP_STATE_OBSTACLE_HOLD ? UI_IND_YELLOW : UI_IND_OFF);
        CHECK(o.indicator == want);
    }
    o = tick(&u, 500, APP_STATE_OBSTACLE_HOLD, 0, 0, false);       // без розширювача індикатор і звук працюють
    CHECK(o.indicator == UI_IND_YELLOW && !o.red && !o.green);
}

// ---------- phase7-telemetry: блимання в OBSTACLE_HOLD (A9–A12) ----------
static ui_out_t tick_occ(ui_core_t *u, uint32_t t, app_state_t s, int l, int r, bool ok, bool front, bool rear)
{
    ui_in_t i = in_of(t, s, l, r, ok);
    i.front_occupied = front;
    i.rear_occupied = rear;
    ui_out_t o;
    ui_core_tick(u, &i, &o);
    return o;
}

// Ровер в ARMED, фари ввімкнені автоматично; вхід в утримання в момент 1000.
static void to_hold(ui_core_t *u)
{
    ui_core_init(u, HZ);
    settle(u, 0, APP_STATE_DISARMED);
    tick(u, 200, APP_STATE_ARMED, 0, 0, true);
}

static void test_blink_front_only(void)
{
    ui_core_t u;
    to_hold(&u);
    ui_out_t o = tick_occ(&u, 1000, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(o.head);                                  // перша фаза «світить»
    CHECK(o.red && o.green);                        // задні ліхтарі без блимання
    o = tick_occ(&u, 1100, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(o.head && o.red && o.green);              // 100 мс від входу — ще перша фаза
    o = tick_occ(&u, 1200, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(o.head);                                  // 200 мс < 250
    o = tick_occ(&u, 1300, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(!o.head && o.red && o.green);             // друга фаза: фари погасли, ліхтарі світять
    o = tick_occ(&u, 1400, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(!o.head);
    o = tick_occ(&u, 1500, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(o.head);                                  // третя фаза (500…749)
}

static void test_blink_rear_only_uses_motion_colour(void)
{
    ui_core_t u;
    to_hold(&u);
    ui_out_t o = tick_occ(&u, 1000, APP_STATE_OBSTACLE_HOLD, 0, 0, true, false, true);
    CHECK(o.red && o.green && o.head);              // стоїть: жовтий у фазі «світить», фари не блимають
    o = tick_occ(&u, 1300, APP_STATE_OBSTACLE_HOLD, 0, 0, true, false, true);
    CHECK(!o.red && !o.green && o.head);            // фаза «вимкнено»: ліхтарі погасли, фари світять
    o = tick_occ(&u, 1500, APP_STATE_OBSTACLE_HOLD, 200, 200, true, false, true);
    CHECK(o.red && o.green);                        // вперед: жовтий
    o = tick_occ(&u, 1600, APP_STATE_OBSTACLE_HOLD, -200, -200, true, false, true);
    CHECK(!o.red && o.green);                       // назад у фазі «світить»: лише зелений
    o = tick_occ(&u, 1800, APP_STATE_OBSTACLE_HOLD, -200, -200, true, false, true);
    CHECK(!o.red && !o.green);                      // фаза «вимкнено»
}

static void test_blink_both_in_step_and_switch_inside_hold(void)
{
    ui_core_t u;
    to_hold(&u);
    ui_out_t o = tick_occ(&u, 1000, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, true);
    CHECK(o.head && o.red && o.green);
    o = tick_occ(&u, 1300, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, true);
    CHECK(!o.head && !o.red && !o.green);           // синхронно
    o = tick_occ(&u, 1400, APP_STATE_OBSTACLE_HOLD, 0, 0, true, false, true);
    CHECK(o.head && !o.red && !o.green);            // передній звільнився: фари світять одразу, задні ще блимають
    o = tick_occ(&u, 1500, APP_STATE_OBSTACLE_HOLD, 0, 0, true, false, false);
    CHECK(o.head && o.red && o.green);              // жоден не позначений (гонка знімків): без блимання
    CHECK(o.indicator == UI_IND_YELLOW);            // жовтий лишається
}

static void test_blink_keeps_manual_head_state(void)
{
    ui_core_t u;
    to_hold(&u);
    CHECK(ui_core_light(&u, UI_CH_HEAD, false, APP_STATE_ARMED, true) == UI_OK);   // Розробник вимкнув фари
    tick(&u, 900, APP_STATE_ARMED, 0, 0, true);
    ui_out_t o = tick_occ(&u, 1000, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(o.head);                                  // блимають як попередження, навіть вимкнені
    o = tick_occ(&u, 1300, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(!o.head);
    o = tick_occ(&u, 1500, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(o.head);
    o = tick_occ(&u, 1600, APP_STATE_OBSTACLE_HOLD, 0, 0, true, false, false);
    CHECK(!o.head);                                 // блимання скінчилось: повернувся ручний стан (вимкнено)
    o = tick_occ(&u, 2000, APP_STATE_ARMED, 0, 0, true, false, false);
    CHECK(!o.head);
    // фари були ввімкнені — після блимання світять
    ui_core_t v;
    to_hold(&v);
    tick_occ(&v, 1000, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    o = tick_occ(&v, 1300, APP_STATE_OBSTACLE_HOLD, 0, 0, true, true, false);
    CHECK(!o.head);
    o = tick_occ(&v, 1400, APP_STATE_OBSTACLE_HOLD, 0, 0, true, false, false);
    CHECK(o.head);
}

static void test_blink_without_expander_and_outside_hold(void)
{
    ui_core_t u;
    to_hold(&u);
    ui_out_t o = tick_occ(&u, 1000, APP_STATE_OBSTACLE_HOLD, 0, 0, false, true, true);
    CHECK(!o.red && !o.green);                      // розширювач недоступний: ліхтарі не керуються
    CHECK(o.head);                                  // ручний стан фар, блимання не накладається
    o = tick_occ(&u, 1300, APP_STATE_OBSTACLE_HOLD, 0, 0, false, true, true);
    CHECK(o.head);                                  // не блимає й далі
    CHECK(o.indicator == UI_IND_YELLOW);
    // поза утриманням прапорці секторів світла не змінюють
    ui_core_t v;
    to_hold(&v);
    o = tick_occ(&v, 300, APP_STATE_ARMED, 0, 0, true, true, true);
    CHECK(o.head && o.red && o.green);
}

static void test_hold_manual_denied(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    CHECK(ui_core_light(&u, UI_CH_HEAD, true, APP_STATE_OBSTACLE_HOLD, true) == UI_DENY_STATE);
    CHECK(ui_core_beep(&u, 1000, 100, 0, APP_STATE_OBSTACLE_HOLD, NULL) == UI_DENY_STATE);
}

// ---------- phase8 T4: зелений індикатор за режимом контролю й охороною ----------
static ui_out_t tick_ctl(ui_core_t *u, uint32_t t, app_state_t s, bool ctl_on, bool guarding)
{
    ui_in_t i = in_of(t, s, 0, 0, true);
    i.tof_control_on = ctl_on;
    i.tof_guarding = guarding;
    ui_out_t o;
    ui_core_tick(u, &i, &o);
    return o;
}

static void test_green_indicator_table(void)
{
    // AC-05/AC-06/AC-07: усі стани × (режим, охорона)
    for (int i = 0; i < APP_STATE_COUNT; i++) {
        for (int ctl = 0; ctl < 2; ctl++) {
            for (int g = 0; g < 2; g++) {
                ui_core_t u;
                ui_core_init(&u, HZ);
                app_state_t s = ALL[i];
                ui_out_t o = tick_ctl(&u, 100, s, ctl, g);
                ui_indicator_t want = UI_IND_OFF;
                if (s == APP_STATE_FAILSAFE) {
                    want = UI_IND_RED;
                } else if (s == APP_STATE_OBSTACLE_HOLD) {
                    want = UI_IND_YELLOW;
                } else if ((s == APP_STATE_DISARMED || s == APP_STATE_ARMED) && ctl && g) {
                    want = UI_IND_GREEN;
                }
                CHECK(o.indicator == want);
            }
        }
    }
}

static void test_green_follows_mode_next_step(void)
{
    ui_core_t u;
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    ui_out_t o = tick_ctl(&u, 100, APP_STATE_DISARMED, true, true);
    CHECK(o.indicator == UI_IND_GREEN);
    o = tick_ctl(&u, 200, APP_STATE_DISARMED, false, false);          // тумблер вимкнено: гасне на наступному кроці
    CHECK(o.indicator == UI_IND_OFF);
    o = tick_ctl(&u, 300, APP_STATE_DISARMED, true, false);           // режим є, охорони немає: не зелений
    CHECK(o.indicator == UI_IND_OFF);
    o = tick_ctl(&u, 400, APP_STATE_DISARMED, true, true);
    CHECK(o.indicator == UI_IND_GREEN);
    o = tick_ctl(&u, 500, APP_STATE_ARMED, true, true);
    CHECK(o.indicator == UI_IND_GREEN);
    o = tick_ctl(&u, 600, APP_STATE_FAILSAFE, true, true);            // червоний сильніший
    CHECK(o.indicator == UI_IND_RED);
    // зелений не змінює світло задніх ліхтарів і зумер
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    ui_out_t a = tick_ctl(&u, 100, APP_STATE_DISARMED, false, false);
    ui_core_init(&u, HZ);
    settle(&u, 0, APP_STATE_DISARMED);
    ui_out_t b = tick_ctl(&u, 100, APP_STATE_DISARMED, true, true);
    CHECK(a.head == b.head && a.red == b.red && a.green == b.green && a.buzzer_on == b.buzzer_on);
    // розширювач недоступний — індикатор працює
    ui_in_t in = in_of(200, APP_STATE_DISARMED, 0, 0, false);
    in.tof_control_on = true;
    in.tof_guarding = true;
    ui_out_t o2;
    ui_core_tick(&u, &in, &o2);
    CHECK(o2.indicator == UI_IND_GREEN);
}

int main(void)
{
    test_green_indicator_table();
    test_green_follows_mode_next_step();
    test_permissions();
    test_manual_light();
    test_rear_by_motion();
    test_head_and_leave_armed();
    test_indicator();
    test_manual_beep();
    test_beep_replace();
    test_beep_range_and_state();
    test_transition_signals();
    test_signal_replaces_manual_and_slot();
    test_slot_cleared_on_failsafe();
    test_no_signal_without_calibration();
    test_failsafe_timeline();
    test_failsafe_without_expander();
    test_silence_outside_failsafe();
    test_failsafe_exit_to_disarmed();
    test_failsafe_reentry_restarts_timeline();
    test_hold_rear_auto_and_double_beep_transitions();
    test_hold_does_not_force_headlights();
    test_disarmed_to_hold_double_beep_replaces_arming();
    test_hold_exits();
    test_double_beep_tolerates_tick_jitter();
    test_double_beep_no_overlap();
    test_double_beep_replaced_by_disarm_signal();
    test_double_beep_manual_beep_waits_in_slot();
    test_hold_indicator_and_no_calibration();
    test_blink_front_only();
    test_blink_rear_only_uses_motion_colour();
    test_blink_both_in_step_and_switch_inside_hold();
    test_blink_keeps_manual_head_state();
    test_blink_without_expander_and_outside_hold();
    test_hold_manual_denied();
    return test_summary();
}
