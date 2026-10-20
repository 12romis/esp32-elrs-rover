---
id: T6
title: "Перевести в drive_core усі перевірки «лише ARMED» на предикат: відсічка, втрата зв'язку, пульт, роззброєння, вікно команди, регулятор"
layer: "app"
deps: ["T5"]
blocks: ["T12"]
acs: ["AC-12", "AC-14", "AC-15"]
files_hint: ["components/drive/drive_core.c", "components/drive/include/drive_core.h", "components/pid/pid_core.c", "test/host/test_drive.c"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "done"
step: "COMMIT"
---

# T6 — Перевести в drive_core усі перевірки «лише ARMED» на предикат: відсічка, втрата зв'язку, пульт, роззброєння, вікно команди, регулятор

## Place in the sequence

- **Blocked by:** T5 — Вбудувати в drive_ctl_step знімок далекомірів: обмеження тяги після мікшера, запити ARMED ↔ OBSTACLE_HOLD, застарілий знімок · **Blocks:** T12 — Підключити далекоміри до drive_hal: знімок у кроці, запит OBSTACLE_HOLD ↔ ARMED без pid_hal_arm_begin, журнал, предикат у журналі й лічильнику затримки · **Wave:** 4.
- **Lane:** спільні файли: T5 (`components/drive/drive_core.c`, `components/drive/include/drive_core.h`, `test/host/test_drive.c`) — серіалізовано.

## Why (user story)

> **As a** Розробник
> **I want** щоб у OBSTACLE_HOLD діяли відсічка по напрузі, втрата звʼязку, роззброєння та вся решта правил ARMED, а світло й регулятор працювали як зазвичай
> **So that** новий стан не стає лазівкою в захисті ровера
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Ця задача гарантує, що в OBSTACLE_HOLD діють усі інваріанти безпеки ARMED і працюють регулятор, трасування та стендова команда руху.

## Inlined context

> **Chosen:** варіант 1. `app_state_is_driving(state)` істинний для ARMED і OBSTACLE_HOLD; `drive_core`, `drive_hal` (журнал роззброєння, `last_stop`, лічильник затримки «кадр → борти»), `console_core`, `pid` (скидання, трасування, блокування команд) і `ui_core` використовують його там, де йдеться про рух. Прямі порівняння з ARMED лишаються лише для озброєння (вхід з DISARMED, фіксація режиму регулятора, фари при вході).
>
> — `adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md, Decision outcome, abridged` · full text: [adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md](../adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md)

> - У фазах 1–5 усе, що стосується руху, прив'язане до ARMED, бо OBSTACLE_HOLD був недосяжним. Перевірено в коді; тепер кожне з цих місць має діяти й в OBSTACLE_HOLD (AC-12, AC-14, AC-15), інакше відповідні критерії хибні: у `drive_core.c` поза ARMED виходи вимкнені й STBY внизу (отже, у OBSTACLE_HOLD ровер не зміг би здати назад, а STBY піднімався б заново при поверненні в ARMED, усупереч AC-13 і пункту 7 конституції), скидається регулятор, запит роззброєння й тумблер DISARM діють лише в ARMED, відсічка по напрузі, втрата звʼязку й нове зʼявлення пульта також; у `console_core.c` команда руху приймається лише в ARMED; у `ui_core.c` автоматичний режим задніх ліхтарів діє лише в ARMED і FAILSAFE (у OBSTACLE_HOLD вони згасли б), кожен вхід в ARMED примусово вмикає фари й дає сигнал переходу (при частих ARMED ↔ OBSTACLE_HOLD це було б блиманням і писком), а ручні команди світла й зумера в OBSTACLE_HOLD відхилені.
>
> — `spec.md §1 ¶4, «Розбіжності», четвертий пункт, verbatim` · full text: [spec.md](../spec.md)

> Усі п'ять причин зупинки, що діяли лише в ARMED, тепер діють і в OBSTACLE_HOLD: відсічка по напрузі й втрата зв'язку ведуть у FAILSAFE, поява пульта в стендовому режимі, тумблер DISARM, команда роззброєння і спливання вікна команди руху — у DISARMED одним переходом. У всіх випадках спершу вимикаються виходи і STBY, потім іде запит стану (AC-12).
>
> — `sad.md §6, «Flow 5: інваріанти безпеки в OBSTACLE_HOLD», потік 5, verbatim` · full text: [sad.md](../sad.md)

> - `pid` і трасування: `armed_prev`, скидання регулятора поза рухом, кадр трасування і блокування `pid on/off/save` переходять на предикат «ровер їде»; перехід ARMED ↔ OBSTACLE_HOLD не скидає й не перезахоплює режим регулятора.
>
> — `sad.md §4, журнал припущень design, bullet «pid і трасування», verbatim` · full text: [sad.md](../sad.md)

> **Hard rule:** 7. Інваріанти безпеки, сильніші за будь-яку функціональність:    - STBY піднімається тільки в стані ARMED і тільки свідомо;    - втрата кадрів CRSF довша за 500 мс → ШІМ у нуль, STBY вниз;    - перша операція по I²C після старту — запис `0x00` у PCF8574;    - VBAT < 6.0 В → мотори не запускаються.
>
> — `constitution.md, п. 7, verbatim` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** 14. Шина I²C працює на 100 кГц, підвищувати до 400 кГц заборонено (розділ 4.6 документа `02-FIRMWARE-CONTEXT.md`). Усі періодичні задачі використовують `vTaskDelayUntil`, а не `vTaskDelay` (розділ 8.5).
>
> — `constitution.md, п. 14, verbatim` · full text: [constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/) · [measured.md](../../../measured.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-12 (US-05) — domain invariant

> **Given** ровер у OBSTACLE_HOLD, передній сектор зайнятий
> **When** по черзі в окремих прогонах: напруга акумулятора падає нижче 6.0 В; зв'язок з пультом зникає понад 0.5 с; у стендовому режимі зʼявляється пульт; Розробник вимикає тумблер озброєння чи подає з консолі команду роззброєння; у стендовому режимі команда руху сплила (2 с без повтору)
> **Then** при напрузі нижче 6.0 В ровер переходить у FAILSAFE з причиною «напруга», мотори нуль, STBY внизу, вихід лише апаратним скиданням; при втраті зв'язку — у FAILSAFE з причиною «втрата зв'язку» не пізніше ніж через 0.52 с, як у фазі 3; зʼявлення пульта й спливання команди руху закінчують консольне керування й роззброюють, як в ARMED; роззброєння виконується одним переходом у DISARMED, STBY внизу; жодна з цих реакцій не залежить від того, чи справні далекоміри
>
> — `spec.md §5, AC-12, verbatim` · full text: [spec.md](../spec.md)

### AC-14 (US-05) — cross-context

> **Given** ровер у OBSTACLE_HOLD (передній сектор зайнятий), регулятор швидкості увімкнено, трасування регулятора ввімкнене, пульт керує
> **When** Розробник їде назад стіком, а потім намагається зберегти набір коефіцієнтів регулятора
> **Then** регулятор тримає задану швидкість назад із допуском фази 5 (±5 % після розгону); рядки трасування виводяться раз на 100 мс, як в ARMED (зміна відносно фази 5, де рядки йшли лише в ARMED); задні ліхтарі показують напрямок руху, як в ARMED; фари лишаються такими, якими були (перехід між ARMED і OBSTACLE_HOLD не вмикає їх примусово), і сигнал переходу не звучить; збереження набору відхилено так само, як в ARMED, з поясненням, що ровер озброєний
>
> — `spec.md §5, AC-14, verbatim` · full text: [spec.md](../spec.md)

### AC-15 (US-05) — cross-context

> **Given** стендовий режим (пульта немає), ровер озброєний з консолі, передній сектор зайнятий
> **When** Розробник подає команду руху вперед, а потім команду руху назад
> **Then** на команду вперед обидва борти лишаються з нулем, консоль один раз пояснює, що напрямок заблоковано перешкодою (а не на кожен повтор); на команду назад ровер їде, команда діє 2 с і сама гасне, як у фазі 1; стендове керування захищене тим самим правилом, що й пульт
>
> — `spec.md §5, AC-15, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Знайти в `drive_ctl_step` кожне `== APP_STATE_ARMED` (відсічка по напрузі, втрата зв'язку, нове зʼявлення пульта, запит роззброєння, тумблер DISARM, `DRIVE_REQ_DRIVE`, скидання регулятора, `armed_prev`, кадр трасування) і замінити на `app_state_is_driving`, де йдеться про рух; озброєння (`DRIVE_REQ_ARM`, фіксація режиму регулятора) лишити на DISARMED → ARMED — `components/drive/drive_core.c`
- [ ] Запит роззброєння з OBSTACLE_HOLD дає `state_request == DISARMED` (пара з T3) — `components/drive/drive_core.c`
- [ ] `armed_prev` і режим регулятора: перехід ARMED ↔ OBSTACLE_HOLD не скидає й не перезахоплює — `components/drive/drive_core.c`
- [ ] Кадр трасування накопичується в обох станах руху — `components/drive/drive_core.c`, `components/pid/pid_core.c` (якщо там є прив'язка до ARMED)
- [ ] Тести паритету: кожна причина з потоку 5 в OBSTACLE_HOLD дає ту саму реакцію, що в ARMED — `test/host/test_drive.c`

## Edge cases

| Case | Behaviour |
|---|---|
| OBSTACLE_HOLD, напруга < 6.0 В | FAILSAFE з причиною «напруга», нуль бортам, STBY вниз |
| OBSTACLE_HOLD, понад 0.5 с без кадру каналів | FAILSAFE з причиною «втрата зв'язку» |
| Стенд, OBSTACLE_HOLD, зʼявився пульт | DISARMED, консольне керування закінчено |
| OBSTACLE_HOLD, тумблер у DISARM або команда `disarm` | DISARMED за один перехід, STBY вниз |
| Стендова команда руху сплила (2 с) | Нуль бортам, DISARMED |
| Команда `arm` в OBSTACLE_HOLD | Відхилено: озброєння лише з DISARMED |
| Перехід ARMED → OBSTACLE_HOLD → ARMED | Режим регулятора і накопичена складова не скидаються (скидання лише при зупинці) |
| Трасування в OBSTACLE_HOLD | Кадри раз на 100 мс, як в ARMED |

## Definition of Done

- [ ] `test/host/run_tests.sh` зелений, серед тестів — паритет усіх причин з потоку 5
- [ ] `check_core_headers.sh` чистий
- [ ] `idf.py build` без помилок і нових попереджень
- [ ] Hard rules пунктів 7 і 14 конституції не порушено
- [ ] every Hard Rule inlined above still holds
