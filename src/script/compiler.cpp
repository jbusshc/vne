#include "script/compiler.h"

#include <cstdio>
#include <string>
#include <unordered_map>

#include "core/hash.h"
#include "formats/state.h"

namespace {

u32 push_string(CompiledScriptData* data, const std::string& s) {
    u32 offset = static_cast<u32>(data->string_pool.size());
    data->string_pool.insert(data->string_pool.end(), s.begin(), s.end());
    data->string_pool.push_back('\0');
    return offset;
}

// SPEC.md #9.2: "clave estable archivo:linea:hash". El hash es la parte que de verdad
// protege contra mostrar una traduccion obsoleta (si el texto original cambia, el hash
// cambia, la clave ya no coincide con ninguna traduccion existente); archivo:linea solo
// esta para que un traductor pueda ubicar la linea a simple vista en el catalogo.
std::string catalog_key(const std::string& file_name, u32 line, u32 key_hash) {
    char hex[9];
    std::snprintf(hex, sizeof(hex), "%08x", key_hash);
    return file_name + ":" + std::to_string(line) + ":" + hex;
}

CmpOp negate_cmp_op(CmpOp op) {
    switch (op) {
        case CmpOp::Eq: return CmpOp::Ne;
        case CmpOp::Ne: return CmpOp::Eq;
        case CmpOp::Lt: return CmpOp::Ge;
        case CmpOp::Le: return CmpOp::Gt;
        case CmpOp::Gt: return CmpOp::Le;
        case CmpOp::Ge: return CmpOp::Lt;
    }
    return CmpOp::Eq;
}

}  // namespace

CompileResult compile_instructions(const std::vector<ParsedInstr>& instructions,
                                    const std::string&              file_name,
                                    const SymbolTable&              symbols) {
    CompileResult result;

    // Pase 1: pc de cada instruccion es su indice en el array final (1:1, Label incluido
    // como un Cmd mas). Recolecta la tabla de etiquetas para resolver Jump/Call/JumpIf y
    // las etiquetas destino de las opciones de Choice.
    std::unordered_map<std::string, u32> label_pcs;
    for (u32 i = 0; i < instructions.size(); ++i) {
        if (instructions[i].kind == InstrKind::Label) {
            label_pcs[instructions[i].name] = i;
        }
    }
    auto resolve_label = [&](const std::string& name) -> u32 {
        auto it = label_pcs.find(name);
        // Ya validado por parse_script (etiqueta desconocida = error de compilacion
        // antes de llegar aqui); 0 es un valor seguro si de todos modos faltara, sin
        // usar excepciones (SPEC.md #4).
        return it != label_pcs.end() ? it->second : 0;
    };

    // Los ids salen de la tabla de simbolos del proyecto (M14). Ya NO se detectan colisiones
    // aqui —el detector de M13 (ADR-0063) se retira— porque con ids secuenciales de una tabla
    // **no puede haber colision**: el problema se elimina en vez de vigilarse.
    //
    // Un nombre que no este en la tabla solo puede pasar si el .vnsym se hornea de un
    // conjunto de guiones distinto del que se compila, asi que se reporta como lo que es:
    // un problema del pipeline, no del guion.
    auto symbol_id = [&](SymbolKind kind, const std::string& name, u32 line) -> u16 {
        if (name.empty()) {
            return k_symbol_id_none;
        }
        u16 id = symbols.find(kind, name);
        if (id == k_symbol_id_none) {
            result.errors.push_back(CompileError{
                file_name, line,
                std::string("el nombre de ") + symbol_kind_name(kind) + " '" + name +
                    "' no esta en la tabla de simbolos; vuelve a ejecutar 'sz_bake symbols' "
                    "incluyendo este guion"});
        }
        return id;
    };

    CompiledScriptData& data = result.data;
    data.cmds.reserve(instructions.size() + 1);

    for (const ParsedInstr& instr : instructions) {
        Cmd cmd{};
        switch (instr.kind) {
            case InstrKind::Label:
                cmd.kind             = CmdKind::Label;
                cmd.label.name_hash = fnv1a_u32(instr.name);
                data.labels.push_back(CompiledLabel{cmd.label.name_hash, static_cast<u32>(data.cmds.size())});
                break;
            case InstrKind::Say: {
                cmd.kind = CmdKind::Say;
                cmd.say.speaker_id = instr.speaker.empty()
                                          ? static_cast<u16>(0xFFFFu)
                                          : symbol_id(SymbolKind::Speaker, instr.speaker, instr.line);
                cmd.say.text_id = push_string(&data, instr.text);
                cmd.say.key_hash = fnv1a_u32(instr.text);
                data.catalog_entries.push_back(
                    CatalogEntry{catalog_key(file_name, instr.line, cmd.say.key_hash), instr.text});
                break;
            }
            case InstrKind::Show:
                cmd.kind           = CmdKind::Show;
                cmd.show.actor_id = symbol_id(SymbolKind::Actor, instr.actor, instr.line);
                cmd.show.pose_id  = symbol_id(SymbolKind::Pose, instr.pose, instr.line);
                cmd.show.slot     = instr.slot;
                cmd.show.fade     = instr.fade;
                break;
            case InstrKind::Hide:
                cmd.kind      = CmdKind::Hide;
                cmd.hide.slot = instr.slot;
                cmd.hide.fade = instr.fade;
                break;
            case InstrKind::Bg:
                cmd.kind     = CmdKind::Bg;
                cmd.bg.bg_id = symbol_id(SymbolKind::Bg, instr.bg, instr.line);
                cmd.bg.fade  = instr.fade;
                break;
            case InstrKind::Wait:
                cmd.kind         = CmdKind::Wait;
                cmd.wait.seconds = instr.seconds;
                break;
            case InstrKind::Jump:
                cmd.kind           = CmdKind::Jump;
                cmd.jump.target_pc = resolve_label(instr.name);
                break;
            case InstrKind::End:
                cmd.kind = CmdKind::End;
                break;
            case InstrKind::SetVar:
                cmd.kind             = CmdKind::SetVar;
                cmd.set_var.var_id   = symbol_id(SymbolKind::Var, instr.var, instr.line);
                cmd.set_var.value    = instr.value;
                break;
            case InstrKind::SetFlag:
                cmd.kind             = CmdKind::SetFlag;
                cmd.set_flag.flag_id = symbol_id(SymbolKind::Flag, instr.var, instr.line);
                cmd.set_flag.value   = instr.value != 0 ? 1u : 0u;
                break;
            case InstrKind::AddVar:
                cmd.kind             = CmdKind::AddVar;
                cmd.add_var.var_id   = symbol_id(SymbolKind::Var, instr.var, instr.line);
                cmd.add_var.value    = instr.value;
                break;
            case InstrKind::JumpIf: {
                // M13: la misma instruccion del parser produce JumpIf o JumpIfFlag segun la
                // condicion sea sobre una variable o sobre una bandera. invert_condition
                // (el salto al @else) se aplica de forma distinta en cada caso: negando el
                // operador en una, volteando el valor esperado en la otra.
                if (instr.condition.is_flag) {
                    cmd.kind                       = CmdKind::JumpIfFlag;
                    cmd.jump_if_flag.flag_id = symbol_id(SymbolKind::Flag, instr.condition.var, instr.line);
                    bool expected                  = instr.condition.flag_expected;
                    cmd.jump_if_flag.expected      = (instr.invert_condition ? !expected : expected) ? 1u : 0u;
                    cmd.jump_if_flag.target_pc     = resolve_label(instr.name);
                    break;
                }
                cmd.kind                  = CmdKind::JumpIf;
                cmd.jump_if.var_id        = symbol_id(SymbolKind::Var, instr.condition.var, instr.line);
                cmd.jump_if.op            = instr.invert_condition
                                                 ? negate_cmp_op(instr.condition.op)
                                                 : instr.condition.op;
                cmd.jump_if.rhs           = instr.condition.rhs;
                cmd.jump_if.target_pc     = resolve_label(instr.name);
                break;
            }
            case InstrKind::Choice: {
                if (instr.choice_options.size() > k_max_choice_options) {
                    result.errors.push_back(CompileError{
                        file_name, instr.line,
                        "un @choice admite como mucho " + std::to_string(k_max_choice_options) +
                            " opciones, y este tiene " +
                            std::to_string(instr.choice_options.size()) +
                            " (la UI reserva sitio para un numero fijo de opciones, ver "
                            "k_max_choice_options en formats/cmd.h)"});
                    break;
                }
                cmd.kind                  = CmdKind::Choice;
                cmd.choice.first_option   = static_cast<u32>(data.choice_options.size());
                cmd.choice.option_count   = static_cast<u8>(instr.choice_options.size());
                for (const ParsedChoiceOption& opt : instr.choice_options) {
                    ChoiceOption co{};
                    co.text_id      = push_string(&data, opt.text);
                    co.target_pc    = resolve_label(opt.target);
                    co.has_condition = opt.has_condition ? 1 : 0;
                    if (opt.has_condition) {
                        co.cond_var_id = symbol_id(SymbolKind::Var, opt.condition.var, instr.line);
                        co.cond_op     = opt.condition.op;
                        co.cond_rhs    = opt.condition.rhs;
                    }
                    co.key_hash = fnv1a_u32(opt.text);
                    // instr.line es la linea del propio @choice, no la de cada opcion
                    // individual (ParsedChoiceOption no guarda la suya, ver
                    // parser.h): aproximacion aceptada, solo afecta a donde apunta la
                    // clave en el catalogo para un traductor, no a su estabilidad (el
                    // hash es la parte que importa).
                    data.catalog_entries.push_back(
                        CatalogEntry{catalog_key(file_name, instr.line, co.key_hash), opt.text});
                    data.choice_options.push_back(co);
                }
                break;
            }
            case InstrKind::ChoiceEnd:
                cmd.kind = CmdKind::ChoiceEnd;
                break;
            case InstrKind::Call:
                cmd.kind           = CmdKind::Call;
                cmd.call.target_pc = resolve_label(instr.name);
                break;
            case InstrKind::Return:
                cmd.kind = CmdKind::Return;
                break;
            case InstrKind::Lua:
                cmd.kind            = CmdKind::LuaCall;
                cmd.lua_call.fn_id = push_string(&data, instr.text);
                break;
            case InstrKind::Sfx:
                // instr.sound debe incluir la extension (p. ej. "@sfx puerta_cierra.wav"):
                // a diferencia de @bgm (que resuelve por catalogo, ADR de M6), Sfx guarda
                // el nombre logico completo directo en el string_pool, asi que no hay
                // forma de adivinar el formato del archivo. "ogg/" y no "assets_src/ogg/"
                // desde M11: audio_load lo resuelve contra el backend de assets activo
                // (directorio suelto o .pak, ver vfs/pak.h), no una ruta de archivo.
                cmd.kind        = CmdKind::Sfx;
                cmd.sfx.text_id = push_string(&data, "ogg/" + instr.sound);
                break;
            case InstrKind::Bgm:
                cmd.kind           = CmdKind::Bgm;
                cmd.bgm.track_id   = static_cast<u16>(fnv1a_u32(instr.sound) % 65536u);
                cmd.bgm.fade       = instr.fade;
                break;
            case InstrKind::StopBgm:
                cmd.kind          = CmdKind::StopBgm;
                cmd.stop_bgm.fade = instr.fade;
                break;
            case InstrKind::Move:
                cmd.kind         = CmdKind::Move;
                cmd.move.slot    = instr.slot;
                cmd.move.x       = instr.move_x;
                cmd.move.y       = instr.move_y;
                cmd.move.seconds = instr.seconds;
                break;
            case InstrKind::Transition:
                cmd.kind                    = CmdKind::Transition;
                cmd.transition.transition_kind = instr.transition_kind;
                cmd.transition.seconds         = instr.seconds;
                break;
        }
        data.cmds.push_back(cmd);
    }

    if (data.cmds.empty() || data.cmds.back().kind != CmdKind::End) {
        Cmd end_cmd{};
        end_cmd.kind = CmdKind::End;
        data.cmds.push_back(end_cmd);
    }

    return result;
}

bool write_vnc(const std::string& path, const CompiledScriptData& data) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }

    const u32 magic              = 0x53434E56u;  // 'VNCS' (V,N,C,S en memoria little-endian)
    // v6 (M14): las tablas de nombres por guion de v5 DESAPARECEN. Los nombres viven ahora en
    // la tabla de simbolos del proyecto (.vnsym), que es una sola para todos los guiones, asi
    // que tenerlos ademas aqui era duplicarlos y arriesgarse a que se desincronizaran.
    const u32 version            = 6;
    const u32 cmd_count          = static_cast<u32>(data.cmds.size());
    const u32 string_pool_size   = static_cast<u32>(data.string_pool.size());
    const u32 label_count        = static_cast<u32>(data.labels.size());
    const u32 choice_option_count = static_cast<u32>(data.choice_options.size());

    bool ok = true;
    ok &= std::fwrite(&magic, sizeof(magic), 1, file) == 1;
    ok &= std::fwrite(&version, sizeof(version), 1, file) == 1;
    ok &= std::fwrite(&cmd_count, sizeof(cmd_count), 1, file) == 1;
    ok &= std::fwrite(&string_pool_size, sizeof(string_pool_size), 1, file) == 1;
    ok &= std::fwrite(&label_count, sizeof(label_count), 1, file) == 1;
    ok &= std::fwrite(&choice_option_count, sizeof(choice_option_count), 1, file) == 1;
    if (cmd_count > 0) {
        ok &= std::fwrite(data.cmds.data(), sizeof(Cmd), cmd_count, file) == cmd_count;
    }
    if (string_pool_size > 0) {
        ok &= std::fwrite(data.string_pool.data(), 1, string_pool_size, file) == string_pool_size;
    }
    if (label_count > 0) {
        ok &= std::fwrite(data.labels.data(), sizeof(CompiledLabel), label_count, file) ==
              label_count;
    }
    if (choice_option_count > 0) {
        ok &= std::fwrite(data.choice_options.data(), sizeof(ChoiceOption), choice_option_count,
                           file) == choice_option_count;
    }

    std::fclose(file);
    return ok;
}
