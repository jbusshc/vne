#include "script/symbols.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "script/parser.h"
#include "formats/state.h"

namespace {
constexpr u32 k_kind_count = static_cast<u32>(SymbolKind::Count);
}  // namespace

u32 symbol_kind_capacity(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Var:  return k_max_vars;
        case SymbolKind::Flag: return k_max_flags;
        // Actor, pose, fondo y hablante no indexan ningun array de GameState: se guardan
        // como u16 sueltos (ActorSlot.actor_id, GameState.bg_id, Cmd::say.speaker_id), asi
        // que el tope real es el del tipo. Menos el 0, que esta reservado.
        case SymbolKind::Actor:
        case SymbolKind::Pose:
        case SymbolKind::Bg:
        case SymbolKind::Speaker: return 65535;
        case SymbolKind::Count:   break;
    }
    return 0;
}

const char* symbol_kind_name(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Var:     return "variable";
        case SymbolKind::Flag:    return "bandera";
        case SymbolKind::Actor:   return "actor";
        case SymbolKind::Pose:    return "pose";
        case SymbolKind::Bg:      return "fondo";
        case SymbolKind::Speaker: return "hablante";
        case SymbolKind::Count:   break;
    }
    return "?";
}

u16 SymbolTable::find(SymbolKind kind, const std::string& name) const {
    const std::vector<std::string>& list = names[static_cast<u32>(kind)];
    for (u32 i = 0; i < list.size(); ++i) {
        if (list[i] == name) {
            return static_cast<u16>(i + 1);  // el id 0 esta reservado
        }
    }
    return k_symbol_id_none;
}

u16 SymbolTable::intern(SymbolKind kind, const std::string& name, bool* out_overflow) {
    *out_overflow = false;
    u16 existing  = find(kind, name);
    if (existing != k_symbol_id_none) {
        return existing;
    }
    std::vector<std::string>& list = names[static_cast<u32>(kind)];
    if (list.size() >= symbol_kind_capacity(kind)) {
        *out_overflow = true;
        return k_symbol_id_none;
    }
    list.push_back(name);
    return static_cast<u16>(list.size());  // el primero es el id 1
}

u32 SymbolTable::count(SymbolKind kind) const {
    return static_cast<u32>(names[static_cast<u32>(kind)].size());
}

bool write_vnsym(const std::string& path, const SymbolTable& table) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }

    const u32 magic   = 0x59534E56u;  // 'VNSY'
    const u32 version = 1;
    bool      ok      = std::fwrite(&magic, sizeof(u32), 1, f) == 1;
    ok = ok && std::fwrite(&version, sizeof(u32), 1, f) == 1;
    for (u32 k = 0; k < k_kind_count; ++k) {
        u32 n = static_cast<u32>(table.names[k].size());
        ok    = ok && std::fwrite(&n, sizeof(u32), 1, f) == 1;
    }
    for (u32 k = 0; k < k_kind_count; ++k) {
        for (const std::string& name : table.names[k]) {
            ok = ok && std::fwrite(name.c_str(), 1, name.size() + 1, f) == name.size() + 1;
        }
    }

    std::fclose(f);
    return ok;
}

bool symbols_collect_from_script(const std::vector<ParsedInstr>& instructions,
                                  const std::string& file_name, SymbolTable* table,
                                  std::string* out_error) {
    bool overflow = false;
    auto take     = [&](SymbolKind kind, const std::string& name, u32 line) {
        if (name.empty() || overflow) {
            return;
        }
        table->intern(kind, name, &overflow);
        if (overflow) {
            *out_error = file_name + ":" + std::to_string(line) + ": se paso del tope de " +
                         std::to_string(symbol_kind_capacity(kind)) + " nombres de " +
                         symbol_kind_name(kind) + " distintos (SPEC.md #8.2) al añadir '" +
                         name + "'";
        }
    };

    for (const ParsedInstr& instr : instructions) {
        switch (instr.kind) {
            case InstrKind::Say:
                take(SymbolKind::Speaker, instr.speaker, instr.line);
                break;
            case InstrKind::Show:
                take(SymbolKind::Actor, instr.actor, instr.line);
                take(SymbolKind::Pose, instr.pose, instr.line);
                break;
            case InstrKind::Bg:
                take(SymbolKind::Bg, instr.bg, instr.line);
                break;
            case InstrKind::SetVar:
            case InstrKind::AddVar:
                take(SymbolKind::Var, instr.var, instr.line);
                break;
            case InstrKind::SetFlag:
                take(SymbolKind::Flag, instr.var, instr.line);
                break;
            case InstrKind::JumpIf:
                if (instr.condition.is_flag) {
                    take(SymbolKind::Flag, instr.condition.var, instr.line);
                } else {
                    take(SymbolKind::Var, instr.condition.var, instr.line);
                }
                break;
            case InstrKind::Lua:
                // El cuerpo del @lua tambien declara nombres: ver symbols_collect_from_lua.
                symbols_collect_from_lua(instr.text, table);
                break;
            case InstrKind::Choice:
                for (const ParsedChoiceOption& opt : instr.choice_options) {
                    if (opt.has_condition) {
                        take(SymbolKind::Var, opt.condition.var, instr.line);
                    }
                }
                break;
            default:
                break;
        }
        if (overflow) {
            return false;
        }
    }
    return true;
}

bool read_vnsym(const std::string& path, SymbolTable* out) {
    *out = SymbolTable{};
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return false;
    }
    std::string bytes(static_cast<usize>(size), '\0');
    usize       read = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (read != bytes.size()) {
        return false;
    }

    usize header = sizeof(u32) * (2 + k_kind_count);
    if (bytes.size() < header) {
        return false;
    }
    u32 magic = 0, version = 0;
    std::memcpy(&magic, bytes.data(), sizeof(u32));
    std::memcpy(&version, bytes.data() + sizeof(u32), sizeof(u32));
    if (magic != 0x59534E56u || version != 1) {
        return false;
    }

    u32 counts[k_kind_count];
    std::memcpy(counts, bytes.data() + 2 * sizeof(u32), sizeof(counts));

    usize offset = header;
    for (u32 k = 0; k < k_kind_count; ++k) {
        for (u32 i = 0; i < counts[k]; ++i) {
            usize end = bytes.find('\0', offset);
            if (end == std::string::npos) {
                return false;
            }
            out->names[k].push_back(bytes.substr(offset, end - offset));
            offset = end + 1;
        }
    }
    return true;
}

SymbolTable symbols_for_single_script(const std::vector<ParsedInstr>& instructions) {
    SymbolTable table;
    std::string error;
    symbols_collect_from_script(instructions, "(guion suelto)", &table, &error);
    return table;
}

// Nombres que usa un cuerpo `@lua`. El DSL y Lua comparten variables y banderas, asi que un
// `vn.set_var("oro", 10)` tiene que entrar en la tabla igual que un `@set oro = 10`; si no,
// una variable que solo toca Lua no existiria y `var_id_of` devolveria 0.
//
// Es un escaneo de subcadenas sobre el codigo fuente, no un parser de Lua: busca
// `vn.get_var("`, `vn.set_var("`, `vn.get_flag("` y `vn.set_flag("` y se queda con el
// literal. Mismo criterio que el escaner de TMX (ADR-0044): el subconjunto que este
// proyecto autora, no el lenguaje entero. Un nombre CALCULADO en runtime
// (`vn.set_var(prefijo .. i, 0)`) no se puede ver y no esta soportado — se detecta en
// runtime, donde var_id_of avisa de que ese nombre no existe.
void symbols_collect_from_lua(const std::string& code, SymbolTable* table) {
    struct Pattern { const char* text; SymbolKind kind; };
    // Lua acepta comillas simples y dobles, y los guiones reales usan las dos. Buscar solo
    // las dobles dejaria sin recoger la mitad de los nombres, en silencio.
    const Pattern patterns[] = {
        {"vn.get_var(", SymbolKind::Var},  {"vn.set_var(", SymbolKind::Var},
        {"vn.get_flag(", SymbolKind::Flag}, {"vn.set_flag(", SymbolKind::Flag},
    };

    for (const Pattern& p : patterns) {
        usize len = std::strlen(p.text);
        usize at  = code.find(p.text);
        while (at != std::string::npos) {
            usize quote_at = at + len;
            if (quote_at >= code.size() ||
                (code[quote_at] != '"' && code[quote_at] != '\'')) {
                // Nombre calculado, no literal: no se puede ver desde aqui.
                at = code.find(p.text, at + len);
                continue;
            }
            char  quote = code[quote_at];
            usize start = quote_at + 1;
            usize end   = code.find(quote, start);
            if (end == std::string::npos) {
                break;
            }
            bool overflow = false;
            table->intern(p.kind, code.substr(start, end - start), &overflow);
            at = code.find(p.text, end);
        }
    }
}
