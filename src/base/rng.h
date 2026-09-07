#pragma once
#include "base/types.h"

// xorshift32 determinista sobre GameState::rng_state (SPEC.md #9.4: "vn.random() usa
// rng_state de GameState, no math.random"), para que el rollback no reejecute nada y aun
// asi el estado quede completo: la siguiente tirada solo depende de rng_state, que si se
// serializa. rng_state nunca puede ser 0 (xorshift se queda en 0 para siempre si arranca
// en 0); rng_next lo corrige solo la primera vez que eso pasa.
u32 rng_next(u32* state);

// Entero en [0, exclusive_max). exclusive_max == 0 siempre devuelve 0.
u32 rng_range(u32* state, u32 exclusive_max);
