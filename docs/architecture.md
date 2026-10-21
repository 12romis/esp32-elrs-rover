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
