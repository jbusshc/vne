#pragma once
#include <cstdlib>

#include "base/log.h"

// VN_ASSERT permanece activo en todas las configuraciones, incluida Ship: detecta
// corrupcion de memoria y otros invariantes que no se pueden dejar pasar silenciosamente
// (SPEC.md #4). No usar para validar entrada de usuario ni fallos esperables de assets.

#define VN_ASSERT(cond, msg)                                                                 \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            log_error("assert fallido: %s (%s:%d) — %s", #cond, __FILE__, __LINE__, (msg));  \
            std::abort();                                                                    \
        }                                                                                     \
    } while (0)
