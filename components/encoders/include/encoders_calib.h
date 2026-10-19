// Виміряні константи енкодерів (конституція, п. 3, 13): значення беруться лише з docs/measured.md.
#pragma once

// Імпульси на оберт колеса, ×4 квадратурна роздільність (docs/measured.md, «ENC_PULSES_PER_REV», P6, виміряно: 2026-10-17).
#define ENC_PULSES_PER_REV 2772

// Множник, що робить рух ровера вперед додатним, у порядку FL, FR, RL, RR
// (docs/measured.md, «ENC_DIR_SIGN[4] підтверджено» і «Підтвердження ENC_DIR_SIGN[4] на ходу», P6/P7, виміряно: 2026-10-17).
#define ENC_DIR_SIGN { +1, -1, +1, -1 }

// Діаметр колеса, мм (docs/measured.md, «WHEEL_DIAMETER_MM», штангенциркуль, виміряно: 2026-10-19).
#define WHEEL_DIAMETER_MM 44
