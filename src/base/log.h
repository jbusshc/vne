#pragma once

// Logging minimo con formato estilo printf. Sin iostream, sin asignacion dinamica.
// No usar dentro del bucle de frame en builds Ship (queda compilado pero no debe llamarse
// desde rutas calientes).

#if defined(__GNUC__) || defined(__clang__)
#define VN_PRINTF_FMT(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define VN_PRINTF_FMT(fmt_index, first_arg)
#endif

void log_info(const char* fmt, ...) VN_PRINTF_FMT(1, 2);
void log_warn(const char* fmt, ...) VN_PRINTF_FMT(1, 2);
void log_error(const char* fmt, ...) VN_PRINTF_FMT(1, 2);
