#include "base/log.h"

#include <cstdarg>
#include <cstdio>

// stdout/stderr llegan aqui completamente bufferizados cuando la salida no es una consola
// (por ejemplo, redirigida a un archivo). Sin flush explicito, un proceso terminado a la
// fuerza pierde todo el log acumulado, lo cual hace inutil este modulo para depuracion.
static void log_write(FILE* stream, const char* level, const char* fmt, va_list args) {
    std::fprintf(stream, "[%s] ", level);
    std::vfprintf(stream, fmt, args);
    std::fprintf(stream, "\n");
    std::fflush(stream);
}

void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(stdout, "info", fmt, args);
    va_end(args);
}

void log_warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(stdout, "warn", fmt, args);
    va_end(args);
}

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(stderr, "error", fmt, args);
    va_end(args);
}
