// HAL сховища звітів у NVS (phase8-session-report, ADR-0004): див. report_store_hal.h.
#include "report_store_hal.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "report";

#define NS "report"
#define KEY_META "meta"
static const char *const SLOT_KEY[REPORT_Q_SLOTS] = {"slot0", "slot1"};

static uint32_t s_slot_seq[REPORT_Q_SLOTS];   // номер звіту в слоті, 0 — порожній
static uint32_t s_commits;

uint32_t report_store_commits(void)
{
    return s_commits;
}

esp_err_t report_store_init(report_q_t *q, uint32_t *next_seq)
{
    report_q_init(q);
    memset(s_slot_seq, 0, sizeof(s_slot_seq));
    *next_seq = 1;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    report_q_entry_t found[REPORT_Q_SLOTS];
    size_t nf = 0;
    uint8_t buf[REPORT_RECORD_MAX];
    for (int i = 0; i < REPORT_Q_SLOTS; i++) {
        size_t len = sizeof(buf);
        esp_err_t e = nvs_get_blob(h, SLOT_KEY[i], buf, &len);
        if (e == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        report_record_t r;
        if (e != ESP_OK || !report_record_decode(buf, len, &r)) {
            ESP_LOGW(TAG, "слот звіту %d пошкоджений (%s) — ігнорується", i, esp_err_to_name(e));
            continue;
        }
        s_slot_seq[i] = r.seq;
        found[nf].seq = r.seq;
        found[nf].reason = r.reason;
        nf++;
    }
    report_q_restore(q, found, nf);
    uint32_t stored = 0;
    uint8_t meta[REPORT_META_SIZE];
    size_t mlen = sizeof(meta);
    if (nvs_get_blob(h, KEY_META, meta, &mlen) == ESP_OK && !report_meta_decode(meta, mlen, &stored)) {
        ESP_LOGW(TAG, "метадані звітів пошкоджені — номер сесії відновлено зі слотів");
        stored = 0;
    }
    *next_seq = report_q_next_seq(q, stored);
    nvs_close(h);
    return ESP_OK;
}

static int slot_for_new(void)
{
    int victim = 0;
    for (int i = 0; i < REPORT_Q_SLOTS; i++) {
        if (s_slot_seq[i] == 0) {
            return i;
        }
        if (s_slot_seq[i] < s_slot_seq[victim]) {
            victim = i;
        }
    }
    return victim;
}

esp_err_t report_store_put(uint32_t seq, session_end_t reason, const char *json, size_t len, uint32_t next_seq)
{
    uint8_t rec[REPORT_RECORD_MAX];
    size_t n = report_record_encode(rec, sizeof(rec), seq, reason, json, len);
    if (n == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    int slot = slot_for_new();
    uint8_t meta[REPORT_META_SIZE];
    report_meta_encode(meta, next_seq);
    err = nvs_set_blob(h, SLOT_KEY[slot], rec, n);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_META, meta, sizeof(meta));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);   // один коміт: документ і метадані разом
    }
    nvs_close(h);
    if (err == ESP_OK) {
        s_slot_seq[slot] = seq;
        s_commits++;
    }
    return err;
}

esp_err_t report_store_load(uint32_t seq, char *buf, size_t n, size_t *len)
{
    if (buf == NULL || n == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < REPORT_Q_SLOTS && err != ESP_OK; i++) {
        if (s_slot_seq[i] != seq || seq == 0) {
            continue;
        }
        uint8_t rec[REPORT_RECORD_MAX];
        size_t rl = sizeof(rec);
        esp_err_t e = nvs_get_blob(h, SLOT_KEY[i], rec, &rl);
        report_record_t r;
        if (e != ESP_OK) {
            err = e;
        } else if (!report_record_decode(rec, rl, &r) || r.seq != seq) {
            err = ESP_ERR_INVALID_CRC;
        } else if (r.json_len + 1 > n) {
            err = ESP_ERR_NO_MEM;
        } else {
            memcpy(buf, r.json, r.json_len);
            buf[r.json_len] = '\0';
            if (len != NULL) {
                *len = r.json_len;
            }
            err = ESP_OK;
        }
    }
    nvs_close(h);
    return err;
}

esp_err_t report_store_remove(uint32_t seq)
{
    for (int i = 0; i < REPORT_Q_SLOTS; i++) {
        if (seq != 0 && s_slot_seq[i] == seq) {
            nvs_handle_t h;
            esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
            if (err != ESP_OK) {
                return err;
            }
            err = nvs_erase_key(h, SLOT_KEY[i]);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;
            }
            if (err == ESP_OK) {
                err = nvs_commit(h);
            }
            nvs_close(h);
            if (err == ESP_OK) {
                s_slot_seq[i] = 0;
                s_commits++;
            }
            return err;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
