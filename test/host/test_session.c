// Хостові тести session_core (phase8-session-report T5, T6; spec A5–A10, AC-20, AC-21, AC-25): межі сесії, причини
// завершення, напруга, утримання, режим контролю, пробіг, швидкості бортів і дисбаланс.
#include <math.h>
#include <string.h>

#include "session_core.h"
#include "test_support.h"

static bool near_(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

// Крок із типовими значеннями: стоїть, напруга 8000 мВ, енкодери доступні, режим контролю ввімкнено.
static session_in_t mk(uint32_t t, app_state_t st)
{
    session_in_t in;
    memset(&in, 0, sizeof(in));
    in.now_ms = t;
    in.state = st;
    in.failsafe_cause = APP_FS_NONE;
    in.next_seq = 7;
    in.battery_valid = true;
    in.battery_mv = 8000;
    in.tof_control_on = true;
    in.hold_nearest = -1;
    in.left_avail = true;
    in.right_avail = true;
    return in;
}

static bool step(session_t *s, session_in_t in, session_summary_t *sum)
{
    return session_step(s, &in, sum);
}

// ---------- T5: життєвий цикл ----------

static void test_lifecycle_disarm(void)
{
    session_t s;
    session_summary_t sum;
    session_init(&s);
    CHECK(!session_active(&s));
    CHECK(!step(&s, mk(100, APP_STATE_DISARMED), &sum));
    CHECK(!session_active(&s));
    session_in_t in = mk(1000, APP_STATE_ARMED);
    in.battery_mv = 8240;
    CHECK(!session_step(&s, &in, &sum));
    CHECK(session_active(&s));
    in = mk(1020, APP_STATE_ARMED);
    in.battery_mv = 8000;
    CHECK(!session_step(&s, &in, &sum));
    in = mk(1040, APP_STATE_ARMED);
    in.battery_mv = 7800;
    CHECK(!session_step(&s, &in, &sum));
    // кінець: DISARMED на наступному кроці
    in = mk(1060, APP_STATE_DISARMED);
    in.battery_mv = 7900;
    CHECK(session_step(&s, &in, &sum));
    CHECK(!session_active(&s));
    CHECK(sum.seq == 7);
    CHECK(sum.duration_ms == 60);
    CHECK(sum.end_reason == SESSION_END_DISARM);
    CHECK(sum.voltage_start_valid && sum.voltage_start_mv == 8240);
    CHECK(sum.voltage_end_valid && sum.voltage_end_mv == 7800);    // остання напруга під час руху
    CHECK(sum.voltage_min_valid && sum.voltage_min_mv == 7800);
    CHECK(sum.hold_count == 0 && sum.hold_total_ms == 0 && !sum.hold_min_valid);
    CHECK(sum.tof_control_off_ms == 0);
    // наступний крок поза сесією нічого не видає
    CHECK(!step(&s, mk(1080, APP_STATE_DISARMED), &sum));
}

static void test_end_reasons(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    session_in_t in = mk(20, APP_STATE_FAILSAFE);
    in.failsafe_cause = APP_FS_LINK_LOST;
    CHECK(session_step(&s, &in, &sum) && sum.end_reason == SESSION_END_LINK_LOST);

    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    in = mk(20, APP_STATE_FAILSAFE);
    in.failsafe_cause = APP_FS_VOLTAGE;
    in.battery_mv = 5800;
    CHECK(session_step(&s, &in, &sum) && sum.end_reason == SESSION_END_VOLTAGE);
    CHECK(sum.voltage_end_mv == 8000);            // остання напруга перед відсічкою, а не 5800

    // з OBSTACLE_HOLD у DISARMED теж кінець
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    step(&s, mk(20, APP_STATE_OBSTACLE_HOLD), &sum);
    CHECK(session_step(&s, &(session_in_t){.now_ms = 40, .state = APP_STATE_DISARMED, .left_avail = true, .right_avail = true,
                                           .hold_nearest = -1}, &sum));
    CHECK(sum.end_reason == SESSION_END_DISARM);
    // FAULT також завершує, як роззброєння
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    CHECK(step(&s, mk(20, APP_STATE_FAULT), &sum) && sum.end_reason == SESSION_END_DISARM);
}

static void test_session_starts_in_hold_and_seq_not_reused(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    session_in_t in = mk(0, APP_STATE_OBSTACLE_HOLD);     // DISARMED → OBSTACLE_HOLD: теж початок
    in.next_seq = 11;
    session_step(&s, &in, &sum);
    CHECK(session_active(&s));
    CHECK(step(&s, mk(20, APP_STATE_DISARMED), &sum) && sum.seq == 11);
    in = mk(100, APP_STATE_ARMED);
    in.next_seq = 12;
    session_step(&s, &in, &sum);
    in = mk(120, APP_STATE_ARMED);
    in.next_seq = 99;                                     // номер під час сесії не змінюється
    session_step(&s, &in, &sum);
    CHECK(step(&s, mk(140, APP_STATE_DISARMED), &sum) && sum.seq == 12);
}

static void test_armed_hold_does_not_split(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    bool ended = false;
    app_state_t seq[] = {APP_STATE_ARMED, APP_STATE_OBSTACLE_HOLD, APP_STATE_ARMED, APP_STATE_OBSTACLE_HOLD, APP_STATE_ARMED};
    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        ended = ended || step(&s, mk(i * 20, seq[i]), &sum);
    }
    CHECK(!ended && session_active(&s));
    CHECK(step(&s, mk(200, APP_STATE_DISARMED), &sum) && sum.duration_ms == 200);
}

static void test_voltage_min_and_invalid(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    session_in_t in = mk(0, APP_STATE_ARMED);
    in.battery_mv = 8200;
    session_step(&s, &in, &sum);
    in = mk(20, APP_STATE_ARMED);
    in.battery_mv = 7000;                                 // просадка під навантаженням
    session_step(&s, &in, &sum);
    in = mk(40, APP_STATE_ARMED);
    in.battery_mv = 7900;
    session_step(&s, &in, &sum);
    CHECK(step(&s, mk(60, APP_STATE_DISARMED), &sum));
    CHECK(sum.voltage_min_valid && sum.voltage_min_mv == 7000 && sum.voltage_end_mv == 7900);

    // знімок напруги застарів на старті: початкова напруга невалідна; мінімум береться з валідних
    session_init(&s);
    in = mk(0, APP_STATE_ARMED);
    in.battery_valid = false;
    session_step(&s, &in, &sum);
    in = mk(20, APP_STATE_ARMED);
    in.battery_mv = 7700;
    session_step(&s, &in, &sum);
    CHECK(step(&s, mk(40, APP_STATE_DISARMED), &sum));
    CHECK(!sum.voltage_start_valid && sum.voltage_min_valid && sum.voltage_min_mv == 7700 && sum.voltage_end_valid);

    // напруга ніколи не була валідною
    session_init(&s);
    in = mk(0, APP_STATE_ARMED);
    in.battery_valid = false;
    session_step(&s, &in, &sum);
    in = mk(20, APP_STATE_ARMED);
    in.battery_valid = false;
    session_step(&s, &in, &sum);
    CHECK(step(&s, mk(40, APP_STATE_DISARMED), &sum));
    CHECK(!sum.voltage_start_valid && !sum.voltage_end_valid && !sum.voltage_min_valid);
}

static void test_holds_counters(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    // перше утримання: U7 і U8 нижче порога, мінімум 140 мм від U8
    session_in_t in = mk(20, APP_STATE_ARMED);
    in.hold_enter = true;
    in.hold_mm = 140;
    in.hold_nearest = 1;
    in.hold_below_mask = (1u << 0) | (1u << 1);
    session_step(&s, &in, &sum);
    step(&s, mk(40, APP_STATE_OBSTACLE_HOLD), &sum);
    step(&s, mk(60, APP_STATE_OBSTACLE_HOLD), &sum);
    step(&s, mk(100, APP_STATE_ARMED), &sum);             // час утримання: 40…100 = 60 мс
    // друге: задній, ближче
    in = mk(120, APP_STATE_ARMED);
    in.hold_enter = true;
    in.hold_mm = 95;
    in.hold_nearest = 3;
    in.hold_below_mask = (1u << 3);
    session_step(&s, &in, &sum);
    step(&s, mk(140, APP_STATE_OBSTACLE_HOLD), &sum);
    // третє: далі, ніж мінімум
    in = mk(160, APP_STATE_OBSTACLE_HOLD);
    in.hold_enter = true;
    in.hold_mm = 150;
    in.hold_nearest = 2;
    in.hold_below_mask = (1u << 2) | (1u << 1);
    session_step(&s, &in, &sum);
    CHECK(step(&s, mk(200, APP_STATE_DISARMED), &sum));
    CHECK(sum.hold_count == 3);
    CHECK(sum.hold_min_valid && sum.hold_min_mm == 95 && sum.hold_min_sensor == 3);
    CHECK(sum.hold_by_sensor[0] == 1 && sum.hold_by_sensor[1] == 2 && sum.hold_by_sensor[2] == 1 && sum.hold_by_sensor[3] == 1);
    // час у OBSTACLE_HOLD: кроки 40(→60: 20), 60(→100: 40), 140(→160: 20), 160(→200: 40) = 120 мс
    CHECK(sum.hold_total_ms == 120);
}

static void test_control_off_time(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    step(&s, mk(20, APP_STATE_ARMED), &sum);
    session_in_t in = mk(40, APP_STATE_ARMED);
    in.tof_control_on = false;
    session_step(&s, &in, &sum);
    in = mk(60, APP_STATE_ARMED);
    in.tof_control_on = false;
    session_step(&s, &in, &sum);
    step(&s, mk(100, APP_STATE_ARMED), &sum);
    CHECK(step(&s, mk(120, APP_STATE_DISARMED), &sum));
    // вимкнений на кроках 40 і 60: інтервали 40→60 і 60→100 за станом попереднього кроку = 20 + 40
    CHECK(sum.tof_control_off_ms == 60);
    // режим вимкнений весь час
    session_init(&s);
    for (uint32_t t = 0; t <= 100; t += 20) {
        in = mk(t, APP_STATE_ARMED);
        in.tof_control_on = false;
        session_step(&s, &in, &sum);
    }
    CHECK(step(&s, mk(120, APP_STATE_DISARMED), &sum) && sum.tof_control_off_ms >= 100);
}

static void test_stationary_session_reported(void)
{
    // озброєно, жодного руху: сесія звітується з нульовим рухом
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    step(&s, mk(20, APP_STATE_ARMED), &sum);
    CHECK(step(&s, mk(40, APP_STATE_DISARMED), &sum));
    CHECK(sum.moving_ms == 0 && sum.distance_valid && sum.distance_cm == 0.0f);
    CHECK(!sum.left_speed_valid && !sum.right_speed_valid && !sum.imbalance_valid);
}

// ---------- T6: пробіг, час руху, швидкості, дисбаланс ----------

// Крок руху: борти з потужністю, швидкості, зміщення за крок, задані значення.
static session_in_t mv(uint32_t t, int pl, int pr, float rl, float rr, float dl, float dr, float cl, float cr)
{
    session_in_t in = mk(t, APP_STATE_ARMED);
    in.left_permille = (int16_t)pl;
    in.right_permille = (int16_t)pr;
    in.left_rpm = rl;
    in.right_rpm = rr;
    in.left_dist_cm = dl;
    in.right_dist_cm = dr;
    in.cmd_left = cl;
    in.cmd_right = cr;
    return in;
}

static void test_straight_one_metre(void)
{
    // AC-20: рівно 1 м прямо: 50 кроків по 2 см на борт
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    for (int i = 1; i <= 50; i++) {
        step(&s, mv((uint32_t)i * 20, 400, 400, 60.0f, 60.0f, 2.0f, 2.0f, 0.4f, 0.4f), &sum);
    }
    CHECK(step(&s, mk(1040, APP_STATE_DISARMED), &sum));
    CHECK(sum.distance_valid && near_(sum.distance_cm, 100.0, 0.5));
    CHECK(sum.moving_ms == 1000);
    CHECK(sum.left_speed_valid && sum.right_speed_valid);
    CHECK(near_(sum.left_avg_rpm, 60.0, 0.01) && near_(sum.right_avg_rpm, 60.0, 0.01));
    CHECK(near_(sum.left_peak_rpm, 60.0, 0.01) && near_(sum.right_peak_rpm, 60.0, 0.01));
    CHECK(sum.imbalance_valid && near_(sum.imbalance_pct, 0.0, 0.01));
}

static void test_turn_in_place_adds_no_distance(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    for (int i = 1; i <= 20; i++) {
        // лівий вперед, правий назад: півсума приростів = 0
        step(&s, mv((uint32_t)i * 20, 500, -500, 80.0f, -80.0f, 1.5f, -1.5f, 0.5f, -0.5f), &sum);
    }
    CHECK(step(&s, mk(440, APP_STATE_DISARMED), &sum));
    CHECK(near_(sum.distance_cm, 0.0, 0.001));
    CHECK(sum.moving_ms == 400);                                 // потужність є — це час руху
    CHECK(near_(sum.left_avg_rpm, 80.0, 0.01) && near_(sum.right_avg_rpm, 80.0, 0.01));   // модуль швидкості
    CHECK(!sum.imbalance_valid);                                 // задані не рівні: прямої ділянки не було
}

static void test_pushed_by_hand_not_counted(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    for (int i = 1; i <= 10; i++) {
        // потужності немає, а колеса крутяться (штовхання, накат)
        step(&s, mv((uint32_t)i * 20, 0, 0, 40.0f, 40.0f, 1.0f, 1.0f, 0, 0), &sum);
    }
    CHECK(step(&s, mk(220, APP_STATE_DISARMED), &sum));
    CHECK(near_(sum.distance_cm, 0.0, 0.001) && sum.moving_ms == 0);
    CHECK(!sum.left_speed_valid && !sum.right_speed_valid);
}

static void test_peak_and_average(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    step(&s, mv(20, 300, 300, 40.0f, 50.0f, 1.0f, 1.0f, 0.3f, 0.3f), &sum);
    step(&s, mv(40, 300, 300, 100.0f, 110.0f, 1.0f, 1.0f, 0.3f, 0.3f), &sum);
    step(&s, mv(60, 300, 300, -70.0f, -60.0f, -1.0f, -1.0f, -0.3f, -0.3f), &sum);   // назад: модуль
    CHECK(step(&s, mk(80, APP_STATE_DISARMED), &sum));
    CHECK(near_(sum.left_peak_rpm, 100.0, 0.01) && near_(sum.right_peak_rpm, 110.0, 0.01));
    CHECK(near_(sum.left_avg_rpm, (40.0 + 100.0 + 70.0) / 3.0, 0.01));
    CHECK(near_(sum.right_avg_rpm, (50.0 + 110.0 + 60.0) / 3.0, 0.01));
}

static void test_imbalance_sign_and_straight_only(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    // пряма: лівий 110, правий 90 → (110−90)/100 = +20 %
    for (int i = 1; i <= 10; i++) {
        step(&s, mv((uint32_t)i * 20, 500, 500, 110.0f, 90.0f, 1.0f, 1.0f, 0.5f, 0.5f), &sum);
    }
    // поворот (задані не рівні): у дисбаланс не входить, хоч швидкості різні
    for (int i = 11; i <= 20; i++) {
        step(&s, mv((uint32_t)i * 20, 600, 200, 200.0f, 20.0f, 1.0f, 1.0f, 0.6f, 0.2f), &sum);
    }
    CHECK(step(&s, mk(420, APP_STATE_DISARMED), &sum));
    CHECK(sum.imbalance_valid && near_(sum.imbalance_pct, 20.0, 0.01));
    // правий швидший → мінус
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    for (int i = 1; i <= 5; i++) {
        step(&s, mv((uint32_t)i * 20, 500, 500, 90.0f, 110.0f, 1.0f, 1.0f, 0.5f, 0.5f), &sum);
    }
    CHECK(step(&s, mk(120, APP_STATE_DISARMED), &sum));
    CHECK(sum.imbalance_valid && near_(sum.imbalance_pct, -20.0, 0.01));
}

static void test_unavailable_board_gives_no_data(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    session_in_t in = mk(0, APP_STATE_ARMED);
    in.right_avail = false;
    session_step(&s, &in, &sum);
    for (int i = 1; i <= 10; i++) {
        in = mv((uint32_t)i * 20, 500, 500, 100.0f, 0.0f, 2.0f, 0.0f, 0.5f, 0.5f);
        in.right_avail = false;
        session_step(&s, &in, &sum);
    }
    in = mk(220, APP_STATE_DISARMED);
    in.right_avail = false;
    CHECK(session_step(&s, &in, &sum));
    CHECK(sum.left_speed_valid && !sum.right_speed_valid);       // правий — «немає даних», не нуль
    CHECK(sum.left_enc_seen && !sum.right_enc_seen);             // звіт пояснює: пробіг лише за лівим бортом
    CHECK(!sum.imbalance_valid);
    CHECK(sum.distance_valid && near_(sum.distance_cm, 20.0, 0.01));   // пробіг за доступним бортом
    // жоден борт без енкодера: пробіг теж «немає даних»
    session_init(&s);
    in = mk(0, APP_STATE_ARMED);
    in.left_avail = in.right_avail = false;
    session_step(&s, &in, &sum);
    in = mv(20, 500, 500, 0, 0, 0, 0, 0.5f, 0.5f);
    in.left_avail = in.right_avail = false;
    session_step(&s, &in, &sum);
    in = mk(40, APP_STATE_DISARMED);
    in.left_avail = in.right_avail = false;
    CHECK(session_step(&s, &in, &sum));
    CHECK(!sum.distance_valid && !sum.left_speed_valid && !sum.right_speed_valid && sum.moving_ms == 20);
}

static void test_sessions_are_independent(void)
{
    session_summary_t sum;
    session_t s;
    session_init(&s);
    step(&s, mk(0, APP_STATE_ARMED), &sum);
    step(&s, mv(20, 500, 500, 100.0f, 100.0f, 5.0f, 5.0f, 0.5f, 0.5f), &sum);
    CHECK(step(&s, mk(40, APP_STATE_DISARMED), &sum) && sum.distance_cm > 4.5f);
    step(&s, mk(100, APP_STATE_ARMED), &sum);
    CHECK(step(&s, mk(120, APP_STATE_DISARMED), &sum));
    CHECK(sum.distance_cm == 0.0f && sum.moving_ms == 0 && sum.hold_count == 0 && !sum.left_speed_valid);
    CHECK(sum.left_enc_seen && sum.right_enc_seen);                // енкодери були доступні, хоч ровер не їхав
}

int main(void)
{
    test_lifecycle_disarm();
    test_end_reasons();
    test_session_starts_in_hold_and_seq_not_reused();
    test_armed_hold_does_not_split();
    test_voltage_min_and_invalid();
    test_holds_counters();
    test_control_off_time();
    test_stationary_session_reported();
    test_straight_one_metre();
    test_turn_in_place_adds_no_distance();
    test_pushed_by_hand_not_counted();
    test_peak_and_average();
    test_imbalance_sign_and_straight_only();
    test_unavailable_board_gives_no_data();
    test_sessions_are_independent();
    return test_summary();
}
