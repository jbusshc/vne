#include "script/lexer.h"

namespace {

std::string_view trim(std::string_view s) {
    usize start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) {
        start += 1;
    }
    usize end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
        end -= 1;
    }
    return s.substr(start, end - start);
}

// Corta en el primer '#' que no este dentro de comillas, para no romper dialogo con un
// '#' literal (p. ej. "Habitacion #3").
std::string_view strip_comment(std::string_view line) {
    bool in_quotes = false;
    for (usize i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            in_quotes = !in_quotes;
        } else if (line[i] == '#' && !in_quotes) {
            return line.substr(0, i);
        }
    }
    return line;
}

}  // namespace

std::vector<SourceLine> lex_lines(std::string_view source) {
    std::vector<SourceLine> lines;

    u32   line_number = 1;
    usize pos         = 0;
    while (pos <= source.size()) {
        usize newline = source.find('\n', pos);
        usize end     = newline == std::string_view::npos ? source.size() : newline;
        std::string_view raw = source.substr(pos, end - pos);

        std::string_view no_comment = strip_comment(raw);
        std::string_view content    = trim(no_comment);
        if (!content.empty()) {
            // Indentacion significativa solo dentro de @if/@choice (SPEC.md #9.2), en
            // multiplos de 4 espacios; un guion mal indentado (p. ej. 2 espacios) redondea
            // hacia abajo en vez de fallar aqui, el parser es quien decide si el nivel
            // resultante tiene sentido en el bloque que esta cerrando o abriendo.
            usize leading = 0;
            while (leading < no_comment.size() && no_comment[leading] == ' ') {
                leading += 1;
            }
            u32 indent = static_cast<u32>(leading / 4);
            lines.push_back(
                SourceLine{line_number, indent, static_cast<u32>(leading), content});
        }

        line_number += 1;
        if (newline == std::string_view::npos) {
            break;
        }
        pos = newline + 1;
    }

    return lines;
}
