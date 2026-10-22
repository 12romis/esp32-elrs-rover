---
id: T8
title: "Створити mqtt_core: кодування CONNECT, PUBLISH (QoS 1), DISCONNECT і розбір CONNACK та PUBACK для MQTT 3.1.1"
layer: "domain"
deps: ["T0"]
blocks: ["T12"]
acs: ["AC-10"]
files_hint: ["components/report/mqtt_core.c", "components/report/include/mqtt_core.h", "test/host/test_mqtt.c"]
owner: "Агент"
estimate: "M"
context_budget: "S"
status: "todo"
step: ""
---

# T8 — Створити mqtt_core: кодування CONNECT, PUBLISH (QoS 1), DISCONNECT і розбір CONNACK та PUBACK для MQTT 3.1.1

## Place in the sequence

- **Blocked by:** T0 — Створити каркаси компонентів session і report: CMake, заголовки й порожні заглушки core/hal, що збираються без попереджень · **Blocks:** T12 — Створити report_net_hal: підключення Wi-Fi, TLS із закріпленим сертифікатом і обмін MQTT з таймаутом 100 мс на крок · **Wave:** 2.
- **Lane:** shares `components/report/mqtt_core.c` with T0 — serialized.

## Why (user story)

> **As a** Розробник
> **I want** щоб після роззброєння ровер сам надсилав на сервер підсумок сесії
> **So that** я бачу історію виїздів і стан акумулятора без підключення USB
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

Задача дає частину історії: створити mqtt_core: кодування CONNECT, PUBLISH (QoS 1), DISCONNECT і розбір CONNACK та PUBACK для MQTT 3.1.1.

## Inlined context

> A15. **Передача.** Успіх — коли сервер підтвердив прийом повідомлення; відсутність підтвердження — невдача. Одна спроба обмежена в часі: від входу в DISARMED до вимкнення Wi-Fi не більше 30 с (пропозиція агента; реального часу підключення ще не виміряно). Після спроби, успішної чи ні, Wi-Fi вимикається одразу. Сервер (сертифікат власний, підписаний власником; кореневий сертифікат кладеться в проєкт) — адреса, порт і ім'я користувача з запиту Розробника (захищене з'єднання, порт 8883), перевірка сертифіката сервера за цим кореневим сертифікатом обовʼязкова; без перевірки дані не надсилаються. Адреса й порт не є секретом, але лежать у тому самому локальному файлі налаштувань, що й секрети.
>
> — `spec.md §1, A15, verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1 (рішення Розробника, 2026-10-22). Кодер/розбірник пакетів — чисті функції, перевіряються хостовими тестами на еталонних байтах; HAL лише відкриває TLS-зʼєднання, пише й читає байти з таймаутом 100 мс на крок (щоб `report` перевіряв стан не рідше ніж раз на 100 мс). Топік `rover/<mac>/session`, QoS 1, `retain` вимкнено, client id `rover-<mac>`, чиста сесія, keepalive не потрібен (зʼєднання коротке); номер сесії у документі відсікає дублікати (spec A12, A17).
>
> — `adr/0002-implement-a-minimal-mqtt-3-1-1-publisher-in-report-core-over-esp-tls.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> Channel `rover/<mac>/session`; QoS 1, `retain` вимкнено; client id `rover-<mac>`, чиста сесія; ім'я користувача й пароль; зʼєднання коротке, keepalive не використовується; повідомлення вважається доставленим, коли сервер підтвердив прийом (PUBACK).
>
> — `contracts/events.md, «Channel», abridged` · full text: [events.md](../contracts/events.md)

> Конституція, п. 6: кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`; логіка живе в core і покривається тестами на хості. Пп. 8–9: збірка без помилок і без нових попереджень; коментарі й коміти — українською, ідентифікатори — англійською.
>
> — `docs/constitution.md, пп. 6, 8, 9, abridged` · full text: [constitution.md](../../../constitution.md)


**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/](../contracts/) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Внутрішнє: байти пакетів MQTT 3.1.1 (CONNECT, PUBLISH QoS 1, PUBACK, CONNACK, DISCONNECT); зовнішній обмін — T12.

## Acceptance criteria

### AC-10 — happy path

> **Given** Wi-Fi і сервер доступні, ровер озброївся з напругою на старті сесії, проїхав і роззброївся
> **When** ровер входить у DISARMED
> **Then** він вмикає Wi-Fi, надсилає один звіт цієї сесії, чекає підтвердження сервера, одразу вимикає Wi-Fi і пише в консоль, що звіт доставлено
>
> — `spec.md §5, AC-10, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `mqtt_core.h/.c` без заголовків IDF: `mqtt_core_build_connect(buf,n,client_id,user,pass,keepalive)`, `mqtt_core_build_publish(buf,n,topic,payload,len,packet_id)` (QoS 1), `mqtt_core_build_disconnect(buf,n)`, `mqtt_core_parse_connack(buf,len,&rc)`, `mqtt_core_parse_puback(buf,len,&packet_id)`; кодування «залишкової довжини» (1–4 байти).
- [ ] Хостові тести `test/host/test_mqtt.c` на еталонних байтах зі стандарту MQTT 3.1.1: CONNECT з користувачем і паролем, PUBLISH QoS 1 з packet id, CONNACK 0 і відмови (коди 4, 5), PUBACK з іншим packet id → відхилення, неповні/обрізані пакети → помилка, пароль не потрапляє ніде, крім пакета CONNECT.

## Edge cases

| Case | Behaviour |
|---|---|
| CONNACK з кодом ≠ 0 | розбір повертає відмову з кодом; обмін зупиняється (T12) |
| PUBACK з чужим packet id | не вважається підтвердженням |
| обрізаний пакет | помилка розбору, без виходу за буфер |
| payload > 1024 байти | кодер відмовляє (документ уже обмежений T7) |

## Definition of Done

- [ ] хостові тести mqtt проходять; `mqtt_core.c` без заголовків ESP-IDF
- [ ] `idf.py build` без нових попереджень
- [ ] кожне Hard Rule вище лишається дійсним
