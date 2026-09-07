#include "script/parser.h"

#include <charconv>

#include "script/lexer.h"

namespace {

std::string_view trim(std::string_view s) {
    usize start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        start += 1;
    }
    usize end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        end -= 1;
    }
    return s.substr(start, end - start);
}

// Sin excepciones en el proyecto (SPEC.md #4): std::from_chars no lanza, a diferencia de
// std::stof/std::stoi.
bool parse_f32(std::string_view s, f32* out) {
    auto res = std::from_chars(s.data(), s.data() + s.size(), *out);
    return res.ec == std::errc() && res.ptr == s.data() + s.size();
}

bool parse_u8(std::string_view s, u8* out) {
    int  value = 0;
    auto res   = std::from_chars(s.data(), s.data() + s.size(), value);
    if (res.ec != std::errc() || res.ptr != s.data() + s.size() || value < 0 || value > 255) {
        return false;
    }
    *out = static_cast<u8>(value);
    return true;
}

std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    usize                         i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') {
            i += 1;
        }
        if (i >= line.size()) {
            break;
        }
        if (line[i] == '"') {
            usize start = i + 1;
            usize end   = line.find('"', start);
            if (end == std::string_view::npos) {
                end = line.size();
            }
            tokens.push_back(line.substr(start, end - start));
            i = end + 1;
        } else {
            usize start = i;
            while (i < line.size() && line[i] != ' ') {
                i += 1;
            }
            tokens.push_back(line.substr(start, i - start));
        }
    }
    return tokens;
}

bool find_kv(const std::vector<std::string_view>& tokens, usize start, std::string_view key,
             std::string_view* out_value) {
    for (usize i = start; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == key) {
            *out_value = tokens[i + 1];
            return true;
        }
    }
    return false;
}

}  // namespace

ParseResult parse_script(std::string_view source, const std::string& file_name) {
    ParseResult result;
    auto        error = [&](u32 line, const std::string& msg) {
        result.errors.push_back(ParseError{file_name, line, msg});
    };

    for (const SourceLine& sl : lex_lines(source)) {
        std::string_view line = sl.text;

        if (line.rfind("::", 0) == 0) {
            ParsedInstr instr;
            instr.kind = InstrKind::Label;
            instr.line = sl.number;
            instr.name = std::string(trim(line.substr(2)));
            result.instructions.push_back(instr);
            continue;
        }

        if (line[0] == '@') {
            std::vector<std::string_view> tokens = tokenize(line);
            std::string_view              cmd    = tokens[0];

            if (cmd == "@end") {
                ParsedInstr instr;
                instr.kind = InstrKind::End;
                instr.line = sl.number;
                result.instructions.push_back(instr);
            } else if (cmd == "@wait") {
                f32 seconds = 0.0f;
                if (tokens.size() < 2 || !parse_f32(tokens[1], &seconds)) {
                    error(sl.number, "@wait espera un numero de segundos");
                    continue;
                }
                ParsedInstr instr;
                instr.kind    = InstrKind::Wait;
                instr.line    = sl.number;
                instr.seconds = seconds;
                result.instructions.push_back(instr);
            } else if (cmd == "@jump") {
                if (tokens.size() < 2) {
                    error(sl.number, "@jump espera el nombre de una etiqueta");
                    continue;
                }
                ParsedInstr instr;
                instr.kind = InstrKind::Jump;
                instr.line = sl.number;
                instr.name = std::string(tokens[1]);
                result.instructions.push_back(instr);
            } else if (cmd == "@bg") {
                if (tokens.size() < 2) {
                    error(sl.number, "@bg espera un nombre de fondo");
                    continue;
                }
                ParsedInstr instr;
                instr.kind = InstrKind::Bg;
                instr.line = sl.number;
                instr.bg   = std::string(tokens[1]);
                std::string_view fade_str;
                if (find_kv(tokens, 2, "fade", &fade_str)) {
                    parse_f32(fade_str, &instr.fade);
                }
                result.instructions.push_back(instr);
            } else if (cmd == "@show") {
                if (tokens.size() < 3) {
                    error(sl.number, "@show espera actor y pose");
                    continue;
                }
                ParsedInstr instr;
                instr.kind  = InstrKind::Show;
                instr.line  = sl.number;
                instr.actor = std::string(tokens[1]);
                instr.pose  = std::string(tokens[2]);
                std::string_view slot_str, fade_str;
                if (find_kv(tokens, 3, "slot", &slot_str)) {
                    parse_u8(slot_str, &instr.slot);
                }
                if (find_kv(tokens, 3, "fade", &fade_str)) {
                    parse_f32(fade_str, &instr.fade);
                }
                result.instructions.push_back(instr);
            } else if (cmd == "@hide") {
                ParsedInstr instr;
                instr.kind = InstrKind::Hide;
                instr.line = sl.number;
                std::string_view slot_str, fade_str;
                if (find_kv(tokens, 1, "slot", &slot_str)) {
                    parse_u8(slot_str, &instr.slot);
                }
                if (find_kv(tokens, 1, "fade", &fade_str)) {
                    parse_f32(fade_str, &instr.fade);
                }
                result.instructions.push_back(instr);
            } else {
                error(sl.number, "comando desconocido: '" + std::string(cmd) + "'");
            }
            continue;
        }

        if (line.front() == '"') {
            if (line.size() < 2 || line.back() != '"') {
                error(sl.number, "cadena de dialogo sin cerrar");
                continue;
            }
            ParsedInstr instr;
            instr.kind = InstrKind::Say;
            instr.line = sl.number;
            instr.text = std::string(line.substr(1, line.size() - 2));
            result.instructions.push_back(instr);
            continue;
        }

        usize colon = line.find(':');
        bool  handled = false;
        if (colon != std::string_view::npos) {
            std::string_view speaker      = trim(line.substr(0, colon));
            bool             valid_speaker = !speaker.empty();
            for (char c : speaker) {
                if (c == ' ' || c == '\t') {
                    valid_speaker = false;
                }
            }
            if (valid_speaker) {
                ParsedInstr instr;
                instr.kind    = InstrKind::Say;
                instr.line    = sl.number;
                instr.speaker = std::string(speaker);
                instr.text    = std::string(trim(line.substr(colon + 1)));
                result.instructions.push_back(instr);
                handled = true;
            }
        }
        if (!handled) {
            error(sl.number, "linea no reconocida");
        }
    }

    // Identificador desconocido = error de compilacion (SPEC.md #9.2): valida saltos
    // contra las etiquetas declaradas en el propio guion.
    std::vector<std::string> labels;
    for (const auto& instr : result.instructions) {
        if (instr.kind == InstrKind::Label) {
            labels.push_back(instr.name);
        }
    }
    auto label_exists = [&](const std::string& name) {
        for (const auto& l : labels) {
            if (l == name) {
                return true;
            }
        }
        return false;
    };
    for (const auto& instr : result.instructions) {
        if (instr.kind == InstrKind::Jump && !label_exists(instr.name)) {
            error(instr.line, "etiqueta desconocida: '" + instr.name + "'");
        }
    }

    return result;
}
