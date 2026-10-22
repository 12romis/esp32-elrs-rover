// HAL мережі звіту (phase8-session-report, ADR-0002, ADR-0003, ADR-0006): див. report_net_hal.h.
#include "report_net_hal.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mqtt_core.h"

// Локальні налаштування: файл у .gitignore, збірка без нього проходить (ADR-0006).
#if defined(__has_include)
#if __has_include("report_secrets.h")
#include "report_secrets.h"
#define REPORT_HAVE_SECRETS 1
#endif
#endif
#ifndef REPORT_HAVE_SECRETS
#define REPORT_WIFI_SSID ""
#define REPORT_WIFI_PASSWORD ""
#define REPORT_MQTT_HOST ""
#define REPORT_MQTT_PORT 8883
#define REPORT_MQTT_USER ""
#define REPORT_MQTT_PASSWORD ""
#endif

#ifdef REPORT_HAVE_ROOT_CA
extern const uint8_t root_ca_pem_start[] asm("_binary_root_ca_pem_start");
extern const uint8_t root_ca_pem_end[] asm("_binary_root_ca_pem_end");
#endif

static const char *TAG __attribute__((unused)) = "report";

#define WIFI_CONNECT_LIMIT_MS 15000   // ліміти спроби (spec A21): Wi-Fi ≤ 15 с, TLS + MQTT ≤ 10 с, усього ≤ 30 с
#define MQTT_LIMIT_MS 10000
#define POLL_MS 20                    // крок опитування стану з'єднання; перевірка «дозволено» — на кожному кроці (≤ 100 мс)
#define WIFI_RETRIES 3

#define BIT_GOT_IP BIT0
#define BIT_FAIL BIT1

static EventGroupHandle_t s_events;
static esp_netif_t *s_netif;
static bool s_wifi_on;
static int s_retries;
static esp_tls_t *s_tls;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool report_net_config_ok(void)
{
#if defined(REPORT_HAVE_SECRETS) && defined(REPORT_HAVE_ROOT_CA)
    return REPORT_WIFI_SSID[0] != '\0' && REPORT_MQTT_HOST[0] != '\0' && REPORT_MQTT_USER[0] != '\0';
#else
    return false;
#endif
}

const char *report_net_config_problem(void)
{
#ifndef REPORT_HAVE_SECRETS
    return "немає файлу report_secrets.h";
#elif !defined(REPORT_HAVE_ROOT_CA)
    return "немає кореневого сертифіката certs/root_ca.pem";
#else
    return report_net_config_ok() ? "" : "порожні SSID, адреса сервера чи ім'я користувача у report_secrets.h";
#endif
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_on && s_retries < WIFI_RETRIES) {
            s_retries++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, BIT_FAIL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, BIT_GOT_IP);
    }
}

void report_net_init(void)
{
    if (s_events != NULL) {
        return;
    }
    s_events = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();    // повторне створення поверне ESP_ERR_INVALID_STATE — це нормально
    s_netif = esp_netif_create_default_wifi_sta();
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
}

uint32_t report_net_free_heap(void)
{
    return esp_get_free_internal_heap_size();
}

void report_net_wifi_down(void)
{
    if (!s_wifi_on) {
        return;
    }
    s_wifi_on = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
}

report_outcome_t report_net_wifi_up(const report_net_t *ctx)
{
    if (!report_net_config_ok() || s_events == NULL) {
        return REPORT_OUT_NO_CONFIG;
    }
    xEventGroupClearBits(s_events, BIT_GOT_IP | BIT_FAIL);
    s_retries = 0;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) {
        return REPORT_OUT_NO_NETWORK;
    }
    s_wifi_on = true;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strlcpy((char *)cfg.sta.ssid, REPORT_WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, REPORT_WIFI_PASSWORD, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;     // відкриті мережі не використовуються (spec §6.1)
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    memset(&cfg, 0, sizeof(cfg));                         // пароль не лишається в стеку
    if (esp_wifi_start() != ESP_OK || esp_wifi_connect() != ESP_OK) {
        report_net_wifi_down();
        return REPORT_OUT_NO_NETWORK;
    }
    uint32_t t0 = now_ms();
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_events, BIT_GOT_IP | BIT_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(POLL_MS));
        if (bits & BIT_GOT_IP) {
            return REPORT_OUT_NONE;
        }
        report_outcome_t fail = REPORT_OUT_NONE;
        if (bits & BIT_FAIL) {
            fail = REPORT_OUT_NO_NETWORK;
        } else if (!ctx->allow()) {
            fail = REPORT_OUT_ABORTED;
        } else if ((uint32_t)(now_ms() - t0) >= WIFI_CONNECT_LIMIT_MS) {
            fail = REPORT_OUT_NO_NETWORK;
        }
        if (fail != REPORT_OUT_NONE) {
            report_net_wifi_down();
            return fail;
        }
    }
}

// Очікування між кроками опитування через vTaskDelayUntil (constitution п. 14): last — мітка попереднього пробудження.
static void poll_wait(TickType_t *last)
{
    vTaskDelayUntil(last, pdMS_TO_TICKS(POLL_MS));
}

static void tls_close(void)
{
    if (s_tls != NULL) {
        esp_tls_conn_destroy(s_tls);
        s_tls = NULL;
    }
}

// Читає рівно один пакет MQTT (фіксований заголовок і залишок) кроками по POLL_MS до deadline_ms. OK — пакет у buf[0..*got).
static report_outcome_t read_packet(const report_net_t *ctx, uint8_t *buf, size_t cap, size_t *got, uint32_t deadline_ms)
{
    size_t have = 0, total = 0;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (!ctx->allow()) {
            return REPORT_OUT_ABORTED;
        }
        if ((int32_t)(now_ms() - deadline_ms) >= 0) {
            return REPORT_OUT_NO_ACK;
        }
        size_t want = (total ? total : 2) - have;   // спершу фіксований заголовок (2 байти), далі решта пакета
        if (want > cap - have) {
            want = cap - have;
        }
        ssize_t r = esp_tls_conn_read(s_tls, buf + have, want);
        if (r > 0) {
            have += (size_t)r;
            if (total == 0) {
                mqtt_status_t st = mqtt_core_packet_len(buf, have, &total);
                if (st == MQTT_MALFORMED || (st == MQTT_OK && total > cap)) {
                    return REPORT_OUT_NO_ACK;
                }
                if (st != MQTT_OK) {
                    total = 0;
                }
            }
            if (total != 0 && have >= total) {
                *got = have;
                return REPORT_OUT_NONE;
            }
            continue;   // є ще байти: читаємо без очікування
        }
        if (r == 0 || (r < 0 && r != ESP_TLS_ERR_SSL_WANT_READ && r != ESP_TLS_ERR_SSL_WANT_WRITE)) {
            return REPORT_OUT_NO_ACK;   // з'єднання закрито сервером чи помилка читання
        }
        poll_wait(&last);
    }
}

// Записує весь пакет кроками; ABORTED, якщо спробу скасовано.
static report_outcome_t write_all(const report_net_t *ctx, const uint8_t *buf, size_t n, uint32_t deadline_ms)
{
    size_t sent = 0;
    TickType_t last = xTaskGetTickCount();
    while (sent < n) {
        if (!ctx->allow()) {
            return REPORT_OUT_ABORTED;
        }
        if ((int32_t)(now_ms() - deadline_ms) >= 0) {
            return REPORT_OUT_NO_ACK;
        }
        ssize_t w = esp_tls_conn_write(s_tls, buf + sent, n - sent);
        if (w > 0) {
            sent += (size_t)w;
        } else if (w < 0 && w != ESP_TLS_ERR_SSL_WANT_READ && w != ESP_TLS_ERR_SSL_WANT_WRITE) {
            return REPORT_OUT_NO_ACK;
        } else {
            poll_wait(&last);
        }
    }
    return REPORT_OUT_NONE;
}

static uint32_t s_mqtt_deadline_ms;

report_outcome_t report_net_mqtt_open(const report_net_t *ctx)
{
#ifndef REPORT_HAVE_ROOT_CA
    (void)ctx;
    return REPORT_OUT_NO_CONFIG;     // без кореневого сертифіката дані не надсилаються
#else
    tls_close();
    s_tls = esp_tls_init();
    if (s_tls == NULL) {
        return REPORT_OUT_NO_NETWORK;
    }
    s_mqtt_deadline_ms = now_ms() + MQTT_LIMIT_MS;
    esp_tls_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cacert_buf = root_ca_pem_start;
    cfg.cacert_bytes = (unsigned int)(root_ca_pem_end - root_ca_pem_start);
    cfg.non_block = true;
    cfg.timeout_ms = MQTT_LIMIT_MS;
    cfg.common_name = REPORT_MQTT_HOST;      // ім'я сервера має збігатися з сертифікатом
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (!ctx->allow()) {
            tls_close();
            return REPORT_OUT_ABORTED;
        }
        int rc = esp_tls_conn_new_async(REPORT_MQTT_HOST, (int)strlen(REPORT_MQTT_HOST), REPORT_MQTT_PORT, &cfg, s_tls);
        if (rc == 1) {
            break;
        }
        if (rc < 0) {
            esp_tls_error_handle_t eh = NULL;
            int low_level = 0, flags = 0;
            esp_err_t last = ESP_OK;
            if (esp_tls_get_error_handle(s_tls, &eh) == ESP_OK && eh != NULL) {
                last = esp_tls_get_and_clear_last_error(eh, &low_level, &flags);   // last — код esp_err, low_level — код mbedtls
            }
            tls_close();
            // Ненульові прапорці перевірки сертифіката або збій розбору кореня — сертифікат недовірений (рев'ю, знахідка 6).
            if (flags != 0 || last == ESP_ERR_MBEDTLS_X509_CRT_PARSE_FAILED) {
                return REPORT_OUT_CERT;
            }
            return REPORT_OUT_NO_NETWORK;
        }
        if ((int32_t)(now_ms() - s_mqtt_deadline_ms) >= 0) {
            tls_close();
            return REPORT_OUT_TIMEOUT;
        }
        poll_wait(&last);
    }
    uint8_t pkt[MQTT_PACKET_MAX];
    char client_id[24];
    snprintf(client_id, sizeof(client_id), "rover-%s", ctx->mac);
    size_t n = mqtt_core_build_connect(pkt, sizeof(pkt), client_id, REPORT_MQTT_USER, REPORT_MQTT_PASSWORD[0] ? REPORT_MQTT_PASSWORD : NULL, 0);
    report_outcome_t o = n ? write_all(ctx, pkt, n, s_mqtt_deadline_ms) : REPORT_OUT_NO_ACK;
    memset(pkt, 0, sizeof(pkt));             // у буфері був пароль
    if (o != REPORT_OUT_NONE) {
        tls_close();
        return o;
    }
    size_t got = 0;
    uint8_t ack[8];
    o = read_packet(ctx, ack, sizeof(ack), &got, s_mqtt_deadline_ms);
    uint8_t rc = 0xFF;
    if (o == REPORT_OUT_NONE && (mqtt_core_parse_connack(ack, got, &rc) != MQTT_OK || rc != MQTT_CONNACK_ACCEPTED)) {
        ESP_LOGW(TAG, "сервер відхилив з'єднання: %s", mqtt_core_connack_text(rc));
        o = REPORT_OUT_NO_ACK;
    }
    if (o != REPORT_OUT_NONE) {
        tls_close();
    }
    return o;
#endif
}

report_outcome_t report_net_mqtt_publish(const report_net_t *ctx, const char *json, size_t len, uint16_t packet_id)
{
    if (s_tls == NULL) {
        return REPORT_OUT_NO_NETWORK;
    }
    uint8_t pkt[MQTT_PACKET_MAX];
    char topic[40];
    snprintf(topic, sizeof(topic), "rover/%s/session", ctx->mac);
    size_t n = mqtt_core_build_publish(pkt, sizeof(pkt), topic, (const uint8_t *)json, len, packet_id);
    if (n == 0) {
        return REPORT_OUT_NO_ACK;
    }
    report_outcome_t o = write_all(ctx, pkt, n, s_mqtt_deadline_ms);
    if (o != REPORT_OUT_NONE) {
        return o;
    }
    size_t got = 0;
    uint8_t ack[8];
    o = read_packet(ctx, ack, sizeof(ack), &got, s_mqtt_deadline_ms);
    if (o != REPORT_OUT_NONE) {
        return o == REPORT_OUT_NO_ACK && (int32_t)(now_ms() - s_mqtt_deadline_ms) >= 0 ? REPORT_OUT_TIMEOUT : o;
    }
    return mqtt_core_puback_matches(ack, got, packet_id) ? REPORT_OUT_DELIVERED : REPORT_OUT_NO_ACK;
}

void report_net_mqtt_close(void)
{
    if (s_tls != NULL) {
        uint8_t pkt[4];
        size_t n = mqtt_core_build_disconnect(pkt, sizeof(pkt));
        if (n > 0) {
            esp_tls_conn_write(s_tls, pkt, n);   // найкращі зусилля: відповіді на DISCONNECT немає
        }
        tls_close();
    }
}
