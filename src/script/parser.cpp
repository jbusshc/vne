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

bool parse_i32(std::string_view s, i32* out) {
    auto res = std::from_chars(s.data(), s.data() + s.size(), *out);
    return res.ec == std::errc() && res.ptr == s.data() + s.size();
}

bool parse_cmp_op(std::string_view s, CmpOp* out) {
    if (s == "==") { *out = CmpOp::Eq; return true; }
    if (s == "!=") { *out = CmpOp::Ne; return true; }
    if (s == "<")  { *out = CmpOp::Lt; return true; }
    if (s == "<=") { *out = CmpOp::Le; return true; }
    if (s == ">")  { *out = CmpOp::Gt; return true; }
    if (s == ">=") { *out = CmpOp::Ge; return true; }
    return false;
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

// Estado compartido entre las funciones de parseo de bloques: errores, contador para
// nombres de etiqueta sinteticos (@if sin nombre propio en el guion), y el resultado.
struct ParserState {
    ParseResult               result;
    u32                       synth_counter = 0;
    const std::vector<SourceLine>* lines = nullptr;

    void error(u32 line, const std::string& msg) {
        result.errors.push_back(ParseError{"", line, msg});
    }

    std::string synth_label(const char* suffix) {
        std::string name = "__synth" + std::to_string(synth_counter) + "_" + suffix;
        return name;
    }
};

void push(ParserState& st, ParsedInstr instr) {
    st.result.instructions.push_back(std::move(instr));
}

// Parsea `var OP valor` (SPEC.md #9.1). Devuelve false y registra un error si el formato
// no encaja.
bool parse_condition_tokens(ParserState& st, const std::vector<std::string_view>& tokens,
                             usize start, u32 line_number, ParsedCondition* out) {
    if (start + 2 >= tokens.size()) {
        st.error(line_number, "condicion mal formada, se esperaba 'variable OP valor'");
        return false;
    }
    out->var = std::string(tokens[start]);
    if (!parse_cmp_op(tokens[start + 1], &out->op)) {
        st.error(line_number,
                  "operador de comparacion desconocido: '" + std::string(tokens[start + 1]) + "'");
        return false;
    }
    if (!parse_i32(tokens[start + 2], &out->rhs)) {
        st.error(line_number, "se esperaba un entero tras el operador de comparacion");
        return false;
    }
    return true;
}

void parse_block(ParserState& st, usize& i, u32 depth);

// Procesa el cuerpo de un @if o de una rama @else: todas las lineas con indent == depth
// hasta la primera linea con indent < depth (el @else/@end que cierra el bloque padre).
void parse_if(ParserState& st, usize& i, u32 depth) {
    const SourceLine& header = (*st.lines)[i];
    std::vector<std::string_view> tokens = tokenize(header.text);
    ParsedCondition cond;
    if (!parse_condition_tokens(st, tokens, 1, header.number, &cond)) {
        i += 1;
        return;
    }
    i += 1;

    std::string else_label = st.synth_label("else");
    std::string end_label  = st.synth_label("end");
    st.synth_counter += 1;

    ParsedInstr jump_if_instr;
    jump_if_instr.kind             = InstrKind::JumpIf;
    jump_if_instr.line             = header.number;
    jump_if_instr.condition        = cond;
    jump_if_instr.invert_condition = true;  // salta a else/end si la condicion NO se cumple
    jump_if_instr.name             = else_label;
    push(st, jump_if_instr);

    // Cuerpo del if, indentado un nivel mas que la cabecera.
    parse_block(st, i, depth + 1);

    ParsedInstr jump_end;
    jump_end.kind = InstrKind::Jump;
    jump_end.line = header.number;
    jump_end.name = end_label;
    push(st, jump_end);

    ParsedInstr else_lbl;
    else_lbl.kind = InstrKind::Label;
    else_lbl.line = header.number;
    else_lbl.name = else_label;
    push(st, else_lbl);

    if (i < st.lines->size() && (*st.lines)[i].indent == depth &&
        (*st.lines)[i].text == "@else") {
        i += 1;
        parse_block(st, i, depth + 1);
    }

    if (i < st.lines->size() && (*st.lines)[i].indent == depth && (*st.lines)[i].text == "@end") {
        i += 1;
    } else {
        st.error(header.number, "'@if' sin '@end' correspondiente");
    }

    ParsedInstr end_lbl;
    end_lbl.kind = InstrKind::Label;
    end_lbl.line = header.number;
    end_lbl.name = end_label;
    push(st, end_lbl);
}

void parse_choice(ParserState& st, usize& i, u32 depth) {
    const SourceLine& header = (*st.lines)[i];
    i += 1;

    ParsedInstr choice_instr;
    choice_instr.kind = InstrKind::Choice;
    choice_instr.line = header.number;

    while (i < st.lines->size() && (*st.lines)[i].indent == depth + 1) {
        const SourceLine& line = (*st.lines)[i];
        if (line.text.empty() || line.text.front() != '"') {
            st.error(line.number, "se esperaba una opcion entre comillas dentro de @choice");
            i += 1;
            continue;
        }
        usize close = line.text.find('"', 1);
        if (close == std::string_view::npos) {
            st.error(line.number, "opcion de @choice sin comillas de cierre");
            i += 1;
            continue;
        }
        ParsedChoiceOption option;
        option.text = std::string(line.text.substr(1, close - 1));

        std::vector<std::string_view> rest_tokens = tokenize(line.text.substr(close + 1));
        usize                         idx         = 0;
        if (idx < rest_tokens.size() && rest_tokens[idx] == "if") {
            idx += 1;
            if (!parse_condition_tokens(st, rest_tokens, idx, line.number, &option.condition)) {
                i += 1;
                continue;
            }
            option.has_condition = true;
            idx += 3;
        }
        if (idx >= rest_tokens.size() || rest_tokens[idx] != "->" || idx + 1 >= rest_tokens.size()) {
            st.error(line.number, "opcion de @choice espera \"texto\" [if var OP valor] -> etiqueta");
            i += 1;
            continue;
        }
        option.target = std::string(rest_tokens[idx + 1]);
        choice_instr.choice_options.push_back(option);
        i += 1;
    }

    if (i < st.lines->size() && (*st.lines)[i].indent == depth && (*st.lines)[i].text == "@end") {
        i += 1;
    } else {
        st.error(header.number, "'@choice' sin '@end' correspondiente");
    }

    push(st, choice_instr);
    ParsedInstr end_instr;
    end_instr.kind = InstrKind::ChoiceEnd;
    end_instr.line = header.number;
    push(st, end_instr);
}

// Procesa todas las lineas con indent == depth hasta la primera con indent < depth (o
// hasta el final del guion). Recursivo: @if/@choice delegan su cuerpo (depth+1) de vuelta
// aqui.
void parse_block(ParserState& st, usize& i, u32 depth) {
    while (i < st.lines->size() && (*st.lines)[i].indent == depth) {
        const SourceLine& sl   = (*st.lines)[i];
        std::string_view  line = sl.text;

        if (line.rfind("::", 0) == 0) {
            ParsedInstr instr;
            instr.kind = InstrKind::Label;
            instr.line = sl.number;
            instr.name = std::string(trim(line.substr(2)));
            push(st, instr);
            i += 1;
            continue;
        }

        if (line[0] == '@') {
            std::vector<std::string_view> tokens = tokenize(line);
            std::string_view              cmd    = tokens[0];

            if (cmd == "@if") {
                parse_if(st, i, depth);
                continue;
            }
            if (cmd == "@choice") {
                parse_choice(st, i, depth);
                continue;
            }
            if (cmd == "@else" || cmd == "@end") {
                // Cierra el bloque padre: no se consume aqui, lo hace parse_if/
                // parse_choice/el nivel superior.
                return;
            }
            if (cmd == "@wait") {
                f32 seconds = 0.0f;
                if (tokens.size() < 2 || !parse_f32(tokens[1], &seconds)) {
                    st.error(sl.number, "@wait espera un numero de segundos");
                } else {
                    ParsedInstr instr;
                    instr.kind    = InstrKind::Wait;
                    instr.line    = sl.number;
                    instr.seconds = seconds;
                    push(st, instr);
                }
            } else if (cmd == "@jump") {
                if (tokens.size() < 2) {
                    st.error(sl.number, "@jump espera el nombre de una etiqueta");
                } else {
                    ParsedInstr instr;
                    instr.kind = InstrKind::Jump;
                    instr.line = sl.number;
                    instr.name = std::string(tokens[1]);
                    push(st, instr);
                }
            } else if (cmd == "@call") {
                if (tokens.size() < 2) {
                    st.error(sl.number, "@call espera el nombre de una etiqueta");
                } else {
                    ParsedInstr instr;
                    instr.kind = InstrKind::Call;
                    instr.line = sl.number;
                    instr.name = std::string(tokens[1]);
                    push(st, instr);
                }
            } else if (cmd == "@return") {
                ParsedInstr instr;
                instr.kind = InstrKind::Return;
                instr.line = sl.number;
                push(st, instr);
            } else if (cmd == "@lua") {
                ParsedInstr instr;
                instr.kind = InstrKind::Lua;
                instr.line = sl.number;
                instr.text = std::string(trim(line.substr(4)));
                push(st, instr);
            } else if (cmd == "@set") {
                if (tokens.size() < 4 || tokens[2] != "=") {
                    st.error(sl.number, "@set espera 'variable = valor'");
                } else {
                    i32 value = 0;
                    if (!parse_i32(tokens[3], &value)) {
                        st.error(sl.number, "@set espera un entero como valor");
                    } else {
                        ParsedInstr instr;
                        instr.kind  = InstrKind::SetVar;
                        instr.line  = sl.number;
                        instr.var   = std::string(tokens[1]);
                        instr.value = value;
                        push(st, instr);
                    }
                }
            } else if (cmd == "@add") {
                if (tokens.size() < 3) {
                    st.error(sl.number, "@add espera 'variable valor'");
                } else {
                    i32 value = 0;
                    if (!parse_i32(tokens[2], &value)) {
                        st.error(sl.number, "@add espera un entero como valor");
                    } else {
                        ParsedInstr instr;
                        instr.kind  = InstrKind::AddVar;
                        instr.line  = sl.number;
                        instr.var   = std::string(tokens[1]);
                        instr.value = value;
                        push(st, instr);
                    }
                }
            } else if (cmd == "@sfx") {
                if (tokens.size() < 2) {
                    st.error(sl.number, "@sfx espera un nombre de sonido");
                } else {
                    ParsedInstr instr;
                    instr.kind  = InstrKind::Sfx;
                    instr.line  = sl.number;
                    instr.sound = std::string(tokens[1]);
                    push(st, instr);
                }
            } else if (cmd == "@bgm") {
                if (tokens.size() < 2) {
                    st.error(sl.number, "@bgm espera un nombre de pista");
                } else {
                    ParsedInstr instr;
                    instr.kind  = InstrKind::Bgm;
                    instr.line  = sl.number;
                    instr.sound = std::string(tokens[1]);
                    std::string_view fade_str;
                    if (find_kv(tokens, 2, "fade", &fade_str)) {
                        parse_f32(fade_str, &instr.fade);
                    }
                    push(st, instr);
                }
            } else if (cmd == "@stopbgm") {
                ParsedInstr instr;
                instr.kind = InstrKind::StopBgm;
                instr.line = sl.number;
                std::string_view fade_str;
                if (find_kv(tokens, 1, "fade", &fade_str)) {
                    parse_f32(fade_str, &instr.fade);
                }
                push(st, instr);
            } else if (cmd == "@bg") {
                if (tokens.size() < 2) {
                    st.error(sl.number, "@bg espera un nombre de fondo");
                } else {
                    ParsedInstr instr;
                    instr.kind = InstrKind::Bg;
                    instr.line = sl.number;
                    instr.bg   = std::string(tokens[1]);
                    std::string_view fade_str;
                    if (find_kv(tokens, 2, "fade", &fade_str)) {
                        parse_f32(fade_str, &instr.fade);
                    }
                    push(st, instr);
                }
            } else if (cmd == "@show") {
                if (tokens.size() < 3) {
                    st.error(sl.number, "@show espera actor y pose");
                } else {
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
                    push(st, instr);
                }
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
                push(st, instr);
            } else if (cmd == "@move") {
                // "@move slot N to X Y in S" (M12): sin find_kv aqui, todas las partes son
                // obligatorias y posicionales con palabras conectoras fijas, a diferencia
                // de "slot"/"fade" en @show/@hide, que son modificadores opcionales.
                bool shape_ok = tokens.size() == 8 && tokens[1] == "slot" && tokens[3] == "to" &&
                                tokens[6] == "in";
                if (!shape_ok) {
                    st.error(sl.number, "@move espera 'slot N to X Y in S'");
                } else {
                    ParsedInstr instr;
                    instr.kind = InstrKind::Move;
                    instr.line = sl.number;
                    bool ok = parse_u8(tokens[2], &instr.slot) &&
                              parse_f32(tokens[4], &instr.move_x) &&
                              parse_f32(tokens[5], &instr.move_y) &&
                              parse_f32(tokens[7], &instr.seconds);
                    if (!ok) {
                        st.error(sl.number, "@move: slot, X, Y y S deben ser numeros");
                    } else {
                        push(st, instr);
                    }
                }
            } else if (cmd == "@transition") {
                if (tokens.size() < 3) {
                    st.error(sl.number, "@transition espera 'fade|wipe|dissolve segundos'");
                } else {
                    ParsedInstr instr;
                    instr.kind = InstrKind::Transition;
                    instr.line = sl.number;
                    if (tokens[1] == "fade") {
                        instr.transition_kind = TransitionKind::Fade;
                    } else if (tokens[1] == "wipe") {
                        instr.transition_kind = TransitionKind::Wipe;
                    } else if (tokens[1] == "dissolve") {
                        instr.transition_kind = TransitionKind::Dissolve;
                    } else {
                        st.error(sl.number,
                                  "@transition: tipo desconocido '" + std::string(tokens[1]) +
                                      "' (fade, wipe o dissolve)");
                        i += 1;
                        continue;
                    }
                    if (!parse_f32(tokens[2], &instr.seconds)) {
                        st.error(sl.number, "@transition espera un numero de segundos");
                    } else {
                        push(st, instr);
                    }
                }
            } else {
                st.error(sl.number, "comando desconocido: '" + std::string(cmd) + "'");
            }
            i += 1;
            continue;
        }

        if (line.front() == '"') {
            if (line.size() < 2 || line.back() != '"') {
                st.error(sl.number, "cadena de dialogo sin cerrar");
                i += 1;
                continue;
            }
            ParsedInstr instr;
            instr.kind = InstrKind::Say;
            instr.line = sl.number;
            instr.text = std::string(line.substr(1, line.size() - 2));
            push(st, instr);
            i += 1;
            continue;
        }

        usize colon   = line.find(':');
        bool  handled = false;
        if (colon != std::string_view::npos) {
            std::string_view speaker       = trim(line.substr(0, colon));
            bool              valid_speaker = !speaker.empty();
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
                push(st, instr);
                handled = true;
            }
        }
        if (!handled) {
            st.error(sl.number, "linea no reconocida");
        }
        i += 1;
    }
}

}  // namespace

ParseResult parse_script(std::string_view source, const std::string& file_name) {
    std::vector<SourceLine> lines = lex_lines(source);

    ParserState st;
    st.lines = &lines;
    usize i  = 0;
    // parse_block se detiene sin consumir en cualquier '@else'/'@end' que encuentre a su
    // propio nivel (son quien cierra un bloque @if/@choice, los consume parse_if/
    // parse_choice). A nivel raiz no hay ningun bloque que cerrar, asi que un '@end' aqui
    // es el terminador del guion (SPEC.md #9.1, ultima linea del ejemplo); cualquier otra
    // cosa que quede sin consumir (incluido un '@else' suelto) es un error real.
    for (;;) {
        parse_block(st, i, 0);
        if (i >= lines.size()) {
            break;
        }
        if (lines[i].text == "@end") {
            ParsedInstr end_instr;
            end_instr.kind = InstrKind::End;
            end_instr.line = lines[i].number;
            push(st, end_instr);
            i += 1;
            continue;
        }
        st.error(lines[i].number, "'" + std::string(lines[i].text) + "' inesperado aqui "
                                   "(cierra un bloque que no esta abierto, o mala indentacion)");
        break;
    }

    ParseResult result = std::move(st.result);
    for (ParseError& e : result.errors) {
        e.file = file_name;
    }

    // Identificador desconocido = error de compilacion (SPEC.md #9.2): valida saltos
    // (Jump, JumpIf sintetico, Call) contra las etiquetas declaradas en el propio guion.
    // Las etiquetas de @choice se validan aparte porque viven en choice_options, no en
    // `name`.
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
        if ((instr.kind == InstrKind::Jump || instr.kind == InstrKind::JumpIf ||
             instr.kind == InstrKind::Call) &&
            !label_exists(instr.name)) {
            result.errors.push_back(
                ParseError{file_name, instr.line, "etiqueta desconocida: '" + instr.name + "'"});
        }
        if (instr.kind == InstrKind::Choice) {
            for (const auto& opt : instr.choice_options) {
                if (!label_exists(opt.target)) {
                    result.errors.push_back(ParseError{
                        file_name, instr.line, "etiqueta desconocida: '" + opt.target + "'"});
                }
            }
        }
    }

    return result;
}
