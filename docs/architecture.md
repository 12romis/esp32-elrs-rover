# Архітектура прошивки

Блок-схема задач і потоків даних. Опис компонентів: `docs/02-FIRMWARE-CONTEXT.md` §6; рішення — `docs/features/<фаза>/adr/`.

## Задачі й потоки даних

```mermaid
flowchart LR
    RX["Приймач ELRS (UART2)"] -->|кадри каналів| crsf["crsf_rx (core 1, prio 6)"]
    crsf -->|знімок каналів| control["control (core 1, prio 5, 20 мс)"]
    battery["battery (200 мс)"] -->|знімок напруги| control
    battery -->|знімок напруги| tel["telemetry (200 мс)"]
    tof["sensors (50 мс): 4 далекоміри"] -->|знімок секторів| control
    enc["encoders: 4 лічильники"] -->|швидкості коліс| control
    console["rover_console (USB)"] -->|запити arm, disarm, drive| control
    control -->|ШІМ, DIR, STBY| motors["мотори (2 борти)"]
    control -->|стан автомата| state["app_state"]
    control -->|борти, прапорці секторів| ui["ui (100 мс)"]
    state --> ui
    ui -->|зумер, індикатор плати| out1["зумер, RGB-індикатор"]
    ui -->|фари, задні ліхтарі| exp["розширювач портів (I2C)"]
    tof -->|лінії скидання| exp
    control -->|час кроку| tel
    tel -->|кадр датчика батареї| crsf
    crsf -->|UART2 TX| RX
    crsf -->|CH9: режим контролю перешкод| control
    control -->|кожен крок: стан, швидкості, потужність| session["session (у кроці керування)"]
    session -->|підсумок сесії, черга на 2| report["report (core 0, prio 1, 100 мс)"]
    report -->|два слоти звітів, номер сесії| nvs[("NVS, простір report")]
    report -->|лише DISARMED, ≤ 30 с| wifi["Wi-Fi → TLS → MQTT"]
    wifi --> broker["MQTT-сервер власника"]
    console -->|report: стан, спроба| report
```

## Автомат станів

```mermaid
stateDiagram-v2
    [*] --> BOOT
    BOOT --> SELFTEST
    SELFTEST --> DISARMED
    SELFTEST --> FAULT
    DISARMED --> ARMED: arm
    DISARMED --> FAILSAFE: аварійна зупинка
    ARMED --> DISARMED: disarm
    ARMED --> OBSTACLE_HOLD: сектор зайнятий
    OBSTACLE_HOLD --> ARMED: сектори вільні
    OBSTACLE_HOLD --> DISARMED: disarm
    ARMED --> FAILSAFE: втрата звʼязку або VBAT нижче 6.0 В
    OBSTACLE_HOLD --> FAILSAFE: втрата звʼязку або VBAT нижче 6.0 В
    FAILSAFE --> DISARMED: звʼязок відновлено
```

Таблиця дозволених переходів має 12 пар із 49 (`components/app_state/app_state_core.c`, `test/host/test_app_state.c`). Діаграма
показує основні шляхи; повна таблиця — у коді.

## Індикація утримання

OBSTACLE_HOLD: подвійний біп на вході та на виході в ARMED; індикатор плати жовтий; передній сектор зайнятий — блимають фари;
задній — задні ліхтарі. Рішення: `docs/features/phase7-telemetry/adr/`.

## Звіт сесії (phase8-session-report)

`control` наприкінці кожного кроку викликає `session_hal_step`: `session_core` веде межі сесії, напругу, утримання, час із вимкненим
контролем, пробіг і швидкості бортів і при завершенні кладе підсумок у чергу з двох елементів без очікування. Задача `report`
(core 0, пріоритет 1, опитування стану раз на 100 мс) у стані без руху переводить підсумки у JSON і пише їх в NVS (один `nvs_commit`),
а на вході в DISARMED із непорожньою чергою піднімає Wi-Fi, публікує звіти від найстаршого (QoS 1) і вимикає Wi-Fi. Озброєння, стан
не DISARMED або 30 с перериває спробу. У BOOT, SELFTEST, ARMED, OBSTACLE_HOLD, FAILSAFE і FAULT радіо вимкнене. Рішення:
`docs/features/phase8-session-report/adr/`.

## Режим контролю перешкод

CH9 → `crsf_tof_control_effective` → `drive_core`: вимкнений режим знімає зайнятість секторів (обмеження тяги й утримання не діють;
OBSTACLE_HOLD виходить у ARMED), інваріанти п. 7 без змін. `ui` показує зеленуватий індикатор за режимом і охороною.
