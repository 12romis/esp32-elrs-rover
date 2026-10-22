// HAL session (phase8-session-report, ADR-0001): див. session_hal.h.
#include "session_hal.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SUMMARY_QUEUE_LEN 2

static QueueHandle_t s_queue;
static session_t s_session;                  // стан сесії: лише задача control
static volatile uint32_t s_next_seq = 1;     // пише control (початок сесії) і report (після відновлення): 32-бітне, атомарне
static volatile bool s_active;               // копія session_active для status

esp_err_t session_hal_init(void)
{
    session_init(&s_session);
    s_queue = xQueueCreate(SUMMARY_QUEUE_LEN, sizeof(session_summary_t));
    return s_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

// Борт: доступний, якщо доступне хоч одне з його коліс. Швидкість — середнє за доступними колесами; зміщення — середнє
// приростів лічильників коліс, доступних і на попередньому кроці, і на цьому (рев'ю phase8, знахідка 7: колесо, що зникло
// посеред сесії, не дає стрибка пробігу).
typedef struct {
    bool avail;
    float rpm;
    float dist_cm;
} board_t;

static int64_t s_prev_accum[ENC_WHEELS];
static bool s_prev_avail[ENC_WHEELS];

static board_t board_of(const enc_state_t *enc, enc_wheel_t a, enc_wheel_t b)
{
    board_t r = {false, 0.0f, 0.0f};
    const enc_wheel_t w[2] = {a, b};
    int n = 0, nd = 0;
    for (int i = 0; i < 2; i++) {
        const enc_wheel_state_t *ws = &enc->w[w[i]];
        if (!ws->available) {
            continue;
        }
        n++;
        r.rpm += (float)enc_core_rpm(ws->speed_pps);
        if (s_prev_avail[w[i]]) {
            r.dist_cm += (float)enc_core_cm(ws->accum - s_prev_accum[w[i]]);
            nd++;
        }
    }
    if (n > 0) {
        r.avail = true;
        r.rpm /= (float)n;
    }
    if (nd > 0) {
        r.dist_cm /= (float)nd;
    }
    return r;
}

void session_hal_step(const session_hal_in_t *h, const enc_state_t *enc)
{
    board_t l = {0}, r = {0};
    if (enc != NULL) {
        l = board_of(enc, ENC_FL, ENC_RL);
        r = board_of(enc, ENC_FR, ENC_RR);
        for (int i = 0; i < ENC_WHEELS; i++) {
            s_prev_avail[i] = enc->w[i].available;
            s_prev_accum[i] = enc->w[i].accum;
        }
    }
    session_in_t in;
    memset(&in, 0, sizeof(in));
    in.now_ms = h->now_ms;
    in.state = h->state;
    in.failsafe_cause = h->failsafe_cause;
    in.next_seq = s_next_seq;
    in.battery_valid = battery_snapshot_usable(&h->battery, h->now_ms);
    in.battery_mv = h->battery.mv;
    in.tof_control_on = h->tof_control_on;
    in.hold_enter = h->hold_enter;
    in.hold_mm = h->hold_mm;
    in.hold_nearest = h->hold_nearest;
    in.hold_below_mask = h->hold_below_mask;
    in.left_permille = h->left_permille;
    in.right_permille = h->right_permille;
    in.left_avail = l.avail;
    in.right_avail = r.avail;
    in.left_rpm = l.rpm;
    in.right_rpm = r.rpm;
    in.left_dist_cm = l.dist_cm;
    in.right_dist_cm = r.dist_cm;
    in.cmd_left = h->cmd_left;
    in.cmd_right = h->cmd_right;

    bool was_active = session_active(&s_session);
    session_summary_t sum;
    bool ended = session_step(&s_session, &in, &sum);
    bool now_active = session_active(&s_session);
    if (!was_active && now_active) {
        s_next_seq = s_next_seq + 1;     // номер, який зайняла сесія, що почалась
    }
    if (ended && s_queue != NULL) {
        if (xQueueSend(s_queue, &sum, 0) != pdTRUE) {
            session_summary_t drop;
            (void)xQueueReceive(s_queue, &drop, 0);   // черга повна: найстаріший затирається, керування не чекає
            (void)xQueueSend(s_queue, &sum, 0);
        }
    }
    s_active = now_active;   // після відправки: «сесія закрита» означає, що підсумок уже в черзі (report чекає на це)
}

void session_hal_raise_next_seq(uint32_t next_seq)
{
    if (next_seq > s_next_seq) {
        s_next_seq = next_seq;
    }
}

uint32_t session_hal_next_seq(void)
{
    return s_next_seq;
}

bool session_hal_receive(session_summary_t *out, uint32_t timeout_ms)
{
    if (s_queue == NULL || out == NULL) {
        return false;
    }
    return xQueueReceive(s_queue, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool session_hal_active(void)
{
    return s_active;
}
