// HAL консолі: читає байти з USB-Serial-JTAG, збирає рядок через console_core, виконує команди.
#include <stdio.h>

#include "app_state_hal.h"
#include "console.h"
#include "console_core.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CONSOLE_STACK 4096
#define CONSOLE_PRIORITY 1
#define CONSOLE_CORE 0
#define REBOOT_FLUSH_MS 200   // скільки чекати спорожнення USB-буфера перед рестартом

static const char *TAG = "console";

static const console_ops_t *s_ops;

static void say(const char *text)
{
    printf("%s\r\n", text);
    fflush(stdout);
}

// Друкує багаторядковий текст, перетворюючи \n на \r\n для терміналу.
static void put_lines(const char *text)
{
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            putchar('\r');
        }
        putchar(*p);
    }
}

// Знімок енкодерів або обнулення (contracts/cli.md). Буфер static — не на стеку консолі, викликає лише вона.
static void do_enc(console_cmd_t cmd)
{
    static char text[640];   // заголовок + 4 рядки коліс ≈ 400 байтів (UTF-8)
    size_t (*op)(char *, size_t) = cmd == CONSOLE_CMD_ENC_RESET ? s_ops->enc_reset : s_ops->enc_snapshot;
    if (op == NULL) {
        say("Енкодери не підключені.");
        return;
    }
    size_t n = op(text, sizeof(text));
    put_lines(text);
    if (n == 0 || text[n - 1] != '\n') {
        printf("\r\n");
    }
    fflush(stdout);
}

// Команда регулятора pid (phase5-pid). Перевірки форми й стану вже пройдено в console_parse.
static void do_pid(console_cmd_t cmd, const char *line)
{
    static char text[700];   // показ стану з двома бортами ≈ 400 байтів (UTF-8)
    if (s_ops->pid_show == NULL) {
        say("Регулятор не підключений.");
        return;
    }
    console_pid_t p = console_parse_pid(line);
    size_t n = 0;
    switch (cmd) {
    case CONSOLE_CMD_PID_SET:
        n = s_ops->pid_set_param(p.param, p.value, text, sizeof(text));
        break;
    case CONSOLE_CMD_PID_MODE:
        n = s_ops->pid_set_mode(p.form == CONSOLE_PID_ON, text, sizeof(text));
        break;
    case CONSOLE_CMD_PID_SAVE:
        n = s_ops->pid_save(text, sizeof(text));
        break;
    case CONSOLE_CMD_PID_TRACE:
        s_ops->pid_trace(p.form == CONSOLE_PID_TRACE_ON);
        n = (size_t)snprintf(text, sizeof(text), "%s", p.form == CONSOLE_PID_TRACE_ON
                             ? "Трасування регулятора ввімкнено: 10 рядків за секунду в ARMED і OBSTACLE_HOLD."
                             : "Трасування регулятора вимкнено.");
        break;
    default:
        n = s_ops->pid_show(text, sizeof(text));
        break;
    }
    put_lines(text);
    if (n == 0 || text[n - 1] != '\n') printf("\r\n");
    fflush(stdout);
}

// Команда далекомірів tof (phase6-tof). Перевірки форми й стану вже пройдено в console_parse.
static void do_tof(console_cmd_t cmd)
{
    static char text[700];   // показ чотирьох датчиків і двох секторів ≈ 600 байтів (UTF-8)
    size_t (*op)(char *, size_t) = cmd == CONSOLE_CMD_TOF_REINIT ? s_ops->tof_reinit : s_ops->tof_show;
    if (op == NULL) {
        say("Далекоміри не підключені.");
        return;
    }
    size_t n = op(text, sizeof(text));
    put_lines(text);
    if (n == 0 || text[n - 1] != '\n') printf("\r\n");
    fflush(stdout);
}

// Команда report (phase8-session-report): стан черги, режим контролю, мережа; у DISARMED — спроба негайно.
static void do_report(void)
{
    if (s_ops->report_view == NULL) {
        say("Звіти сесій не підключені.");
        return;
    }
    console_report_view_t v;
    s_ops->report_view(&v, true);
    static char text[560];   // чотири рядки ≈ 450 байтів (UTF-8)
    console_fmt_report(text, sizeof(text), &v);
    put_lines(text);
    printf("\r\n");
    fflush(stdout);
}

static void do_status(void)
{
    uint64_t up_s = (uint64_t)(esp_timer_get_time() / 1000000);
    printf("стан: %s\r\n", app_state_name(app_state_get()));
    printf("час роботи: %llu с\r\n", (unsigned long long)up_s);
    printf("вільна внутрішня памʼять: %u байт\r\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("причина останнього скидання: %s\r\n", s_ops->reset_reason ? s_ops->reset_reason() : "?");
    char storage[80] = "?";
    if (s_ops->storage_status) {
        s_ops->storage_status(storage, sizeof(storage));
    }
    printf("сховище: %s\r\n", storage);

    app_sm_entry_t hist[APP_SM_HISTORY_MAX];
    size_t n = app_state_history(hist, APP_SM_HISTORY_MAX);
    if (s_ops->motor_status) {
        console_motor_status_t ms;
        s_ops->motor_status(&ms);
        char line[320];  // найдовший рядок «остання зупинка» ≈ 250 байтів (UTF-8)
        console_fmt_motor_status(line, sizeof(line), ms.mv, ms.mv_valid, ms.stby, ms.left_permille,
                                 ms.right_permille, ms.remaining_ms);
        printf("%s\r\n", line);
        console_fmt_last_stop(line, sizeof(line), ms.last_stop_reason, ms.last_stop_min_mv, ms.last_stop_t_ms);
        printf("%s\r\n", line);
    }
    if (s_ops->ui_status) {
        console_ui_status_t us;
        s_ops->ui_status(&us);
        char line[320];  // найдовший рядок ≈ 270 байтів (UTF-8)
        console_fmt_ui_status(line, sizeof(line), us.expander_ok, us.head, us.red, us.green, us.rear_auto,
                              us.indicator, us.buzzer_on, us.remaining_ms);
        printf("%s\r\n", line);
    }
    if (s_ops->report_view) {
        console_report_view_t rv;
        s_ops->report_view(&rv, false);
        char line[320];   // «контроль перешкод …» ≈ 130 байтів, «звіти: …» ≈ 200 байтів (UTF-8)
        console_fmt_tof_control_status(line, sizeof(line), &rv);
        printf("%s\r\n", line);
        console_fmt_report_status(line, sizeof(line), &rv);
        printf("%s\r\n", line);
    }
    if (s_ops->rc_status) {
        static char rc[800];   // рядки про пульт (зокрема вихід з FAILSAFE); static — не на стеку консолі, викликає лише вона
        s_ops->rc_status(rc, sizeof(rc));
        put_lines(rc);
    }
    if (s_ops->step_status) {
        char step[256];   // «крок керування: останній … мкс, …» ≈ 190 байтів (UTF-8)
        s_ops->step_status(step, sizeof(step));
        put_lines(step);
        printf("\r\n");
    }
    if (s_ops->enc_status) {
        char enc[160];   // «енкодери: 0 з 4 доступні, недоступні: FL, FR, RL, RR» ≈ 90 байтів (UTF-8)
        s_ops->enc_status(enc, sizeof(enc));
        put_lines(enc);
        printf("\r\n");
    }
    printf("історія переходів (%u):\r\n", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        printf("  %u мс: %s -> %s\r\n", (unsigned)hist[i].t_ms, app_state_name(hist[i].from),
               app_state_name(hist[i].to));
    }
    fflush(stdout);
}

static void do_motor(console_cmd_t cmd, const char *line)
{
    console_motor_cmd_t mc = cmd == CONSOLE_CMD_ARM ? CONSOLE_MOTOR_ARM
                           : cmd == CONSOLE_CMD_DISARM ? CONSOLE_MOTOR_DISARM : CONSOLE_MOTOR_DRIVE;
    float t = 0.0f, s = 0.0f;
    if (cmd == CONSOLE_CMD_DRIVE) {
        (void)console_parse_drive(line, &t, &s);  // уже перевірено в console_parse
    }
    console_motor_reply_t r = {0};
    if (s_ops->motor_cmd == NULL || !s_ops->motor_cmd(mc, t, s, &r)) {
        say("Керування не відповіло — команду не прийнято.");
        return;
    }
    char buf[480];   // «Прийнято …» ≈ 150 байтів + пояснення про перешкоду ≈ 180 байтів (UTF-8)
    if (!r.accepted) {
        snprintf(buf, sizeof(buf), "Відхилено: %s.", r.text);
    } else if (cmd == CONSOLE_CMD_ARM) {
        snprintf(buf, sizeof(buf), "%s", console_msg_arm_ok());
    } else if (cmd == CONSOLE_CMD_DISARM) {
        snprintf(buf, sizeof(buf), "Мотори вимкнено: %s.", r.text);
    } else {
        char l[16], rr[16];
        console_fmt_permille(l, sizeof(l), r.left_permille);
        console_fmt_permille(rr, sizeof(rr), r.right_permille);
        int w = snprintf(buf, sizeof(buf), "Прийнято: ліве %s, праве %s (діє 2 с, повторіть команду, щоб їхати далі).", l, rr);
        if (r.text[0] != '\0' && w > 0 && (size_t)w < sizeof(buf)) {
            snprintf(buf + w, sizeof(buf) - (size_t)w, " %s", r.text);
        }
    }
    say(buf);
}

static void do_ui(console_cmd_t cmd, const char *line)
{
    console_ui_reply_t r = {0};
    char buf[480];
    bool answered;
    if (cmd == CONSOLE_CMD_LIGHT) {
        ui_channel_t ch = UI_CH_HEAD;
        bool on = false;
        (void)console_parse_light(line, &ch, &on);  // уже перевірено в console_parse
        answered = s_ops->light_cmd != NULL && s_ops->light_cmd(ch, on, &r);
    } else {
        uint32_t hz = 0, dur = 0;
        (void)console_parse_beep(line, &hz, &dur);
        answered = s_ops->beep_cmd != NULL && s_ops->beep_cmd(hz, dur, &r);
    }
    if (!answered) {
        say("Світло й зумер недоступні: керування не стартувало — команду не прийнято.");
        return;
    }
    if (r.result != UI_OK) {
        char why[400];  // найдовший текст відмови ≈ 360 байтів (UTF-8), не обрізається
        console_fmt_ui_denied(why, sizeof(why), r.result, app_state_get(), app_state_failsafe_cause());
        snprintf(buf, sizeof(buf), "Відхилено: %s", why);
    } else if (r.bus_failure) {
        snprintf(buf, sizeof(buf), "Відхилено: збій шини — розширювач не підтвердив запис, стан світла не змінено (head %s, red %s, green %s).",
                 r.head ? "ввімкнено" : "вимкнено", r.red ? "ввімкнено" : "вимкнено", r.green ? "ввімкнено" : "вимкнено");
    } else if (cmd == CONSOLE_CMD_LIGHT) {
        snprintf(buf, sizeof(buf), "Світло: head %s, red %s, green %s.", r.head ? "ввімкнено" : "вимкнено",
                 r.red ? "ввімкнено" : "вимкнено", r.green ? "ввімкнено" : "вимкнено");
    } else {
        uint32_t hz = 0, dur = 0;
        (void)console_parse_beep(line, &hz, &dur);
        snprintf(buf, sizeof(buf), "Сигнал: %u Гц, %u мс%s.", (unsigned)hz, (unsigned)dur,
                 r.queued ? " (прозвучить одразу після сигналу переходу)" : "");
    }
    say(buf);
}

static void execute(const char *line)
{
    console_cmd_t cmd = console_parse(line, app_state_get());
    switch (cmd) {
    case CONSOLE_CMD_EMPTY:
        break;
    case CONSOLE_CMD_UNKNOWN:
        say(console_msg_unknown());
        break;
    case CONSOLE_CMD_EXTRA_ARGS:
        say(console_msg_extra_args());
        break;
    case CONSOLE_CMD_DENIED_STATE: {
        char buf[320];
        if (console_identify(line) == CONSOLE_CMD_PID) {
            console_fmt_pid_denied(buf, sizeof(buf), console_parse_pid(line).form, app_state_get());
        } else if (console_identify(line) == CONSOLE_CMD_TOF) {
            console_fmt_tof_denied(buf, sizeof(buf), app_state_get());
        } else {
            console_fmt_denied(buf, sizeof(buf), console_identify(line), app_state_get(), app_state_failsafe_cause());
        }
        say(buf);
        break;
    }
    case CONSOLE_CMD_BAD_ARGS: {
        console_cmd_t named = console_identify(line);
        if (named == CONSOLE_CMD_LIGHT) {
            ui_channel_t ch;
            bool on;
            say(console_msg_light_error(console_parse_light(line, &ch, &on)));
        } else if (named == CONSOLE_CMD_BEEP) {
            uint32_t hz, dur;
            say(console_msg_beep_error(console_parse_beep(line, &hz, &dur)));
        } else if (named == CONSOLE_CMD_ENC) {
            say(console_msg_enc_error(console_parse_enc(line)));
        } else if (named == CONSOLE_CMD_TOF) {
            static char terr[500];
            console_tof_t tp = console_parse_tof(line);
            console_fmt_tof_error(terr, sizeof(terr), &tp);
            say(terr);
        } else if (named == CONSOLE_CMD_PID) {
            static char perr[700];   // перелік форм ≈ 600 байтів UTF-8
            console_pid_t p = console_parse_pid(line);
            console_fmt_pid_error(perr, sizeof(perr), &p);
            say(perr);
        } else {
            float t, s;
            say(console_msg_drive_error(console_parse_drive(line, &t, &s)));
        }
        break;
    }
    case CONSOLE_CMD_LIGHT:
    case CONSOLE_CMD_BEEP:
        do_ui(cmd, line);
        break;
    case CONSOLE_CMD_ARM:
    case CONSOLE_CMD_DISARM:
    case CONSOLE_CMD_DRIVE:
        do_motor(cmd, line);
        break;
    case CONSOLE_CMD_ENC:
    case CONSOLE_CMD_ENC_RESET:
        do_enc(cmd);
        break;
    case CONSOLE_CMD_PID:
    case CONSOLE_CMD_PID_SET:
    case CONSOLE_CMD_PID_MODE:
    case CONSOLE_CMD_PID_SAVE:
    case CONSOLE_CMD_PID_TRACE:
        do_pid(cmd, line);
        break;
    case CONSOLE_CMD_TOF:
    case CONSOLE_CMD_TOF_REINIT:
        do_tof(cmd);
        break;
    case CONSOLE_CMD_REPORT:
        do_report();
        break;
    case CONSOLE_CMD_HELP:
        say(console_help_text());
        break;
    case CONSOLE_CMD_STATUS:
        do_status();
        break;
    case CONSOLE_CMD_REBOOT:
        say("Перезавантаження плати...");
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(REBOOT_FLUSH_MS));  // щоб повідомлення встигло вийти
        if (s_ops->reboot) {
            s_ops->reboot();
        } else {
            esp_restart();
        }
        break;
    case CONSOLE_CMD_WDT_TEST:
        say("Навмисне зависання службової задачі: плата перезавантажиться приблизно за 5 с "
            "з причиною TASK_WDT.");
        if (s_ops->request_hang) {
            s_ops->request_hang();
        }
        break;
    }
}

static void console_task(void *arg)
{
    (void)arg;
    console_t con;
    console_init(&con);
    for (;;) {
        char ch;
        if (usb_serial_jtag_read_bytes(&ch, 1, portMAX_DELAY) != 1) {
            continue;
        }
        console_event_t ev = console_feed(&con, ch);
        // Ехо: дзеркалимо введене, бо монітор не друкує ввід сам.
        if (ev != CONSOLE_EVENT_NONE) {
            printf("\r\n");
        } else if (ch == 0x08 || ch == 0x7F) {
            printf("\b \b");
        } else if ((unsigned char)ch >= 0x20 || ch == '\t') {
            putchar(ch);
        }
        fflush(stdout);
        if (ev == CONSOLE_EVENT_LINE) {
            execute(console_line(&con));
        } else if (ev == CONSOLE_EVENT_OVERFLOW) {
            say(console_msg_overflow());
        }
    }
}

esp_err_t console_start(const console_ops_t *ops)
{
    if (ops == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_ops = ops;

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    // Вивід (printf, ESP_LOG) теж через драйвер, як у еталонному REPL ESP-IDF.
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreatePinnedToCore(console_task, "console", CONSOLE_STACK, NULL, CONSOLE_PRIORITY, NULL,
                                CONSOLE_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу console");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "консоль готова, наберіть help");
    return ESP_OK;
}
