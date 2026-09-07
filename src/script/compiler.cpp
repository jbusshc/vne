#include "script/compiler.h"

#include <cstdio>
#include <unordered_map>

namespace {

// FNV-1a: determinista, suficiente para un hash de depuracion de nombres de etiqueta (no
// se usa para resolver saltos en runtime, eso ya son pc directos).
u32 fnv1a(const std::string& s) {
    u32 hash = 2166136261u;
    for (char c : s) {
        hash ^= static_cast<u8>(c);
        hash *= 16777619u;
    }
    return hash;
}

class NameInterner {
public:
    u16 intern(const std::string& name) {
        auto it = ids_.find(name);
        if (it != ids_.end()) {
            return it->second;
        }
        u16 id      = static_cast<u16>(ids_.size());
        ids_[name] = id;
        return id;
    }

private:
    std::unordered_map<std::string, u16> ids_;
};

u32 push_string(CompiledScriptData* data, const std::string& s) {
    u32 offset = static_cast<u32>(data->string_pool.size());
    data->string_pool.insert(data->string_pool.end(), s.begin(), s.end());
    data->string_pool.push_back('\0');
    return offset;
}

}  // namespace

CompileResult compile_instructions(const std::vector<ParsedInstr>& instructions,
                                    const std::string&              file_name) {
    CompileResult result;
    (void)file_name;

    // Pase 1: pc de cada instruccion es su indice en el array final (1:1, Label incluido
    // como un Cmd mas). Recolecta la tabla de etiquetas para resolver Jump.
    std::unordered_map<std::string, u32> label_pcs;
    for (u32 i = 0; i < instructions.size(); ++i) {
        if (instructions[i].kind == InstrKind::Label) {
            label_pcs[instructions[i].name] = i;
        }
    }

    NameInterner        actors;
    NameInterner        poses;
    NameInterner        bgs;
    NameInterner        speakers;
    CompiledScriptData& data = result.data;
    data.cmds.reserve(instructions.size() + 1);

    for (const ParsedInstr& instr : instructions) {
        Cmd cmd{};
        switch (instr.kind) {
            case InstrKind::Label:
                cmd.kind             = CmdKind::Label;
                cmd.label.name_hash = fnv1a(instr.name);
                data.labels.push_back(CompiledLabel{cmd.label.name_hash, static_cast<u32>(data.cmds.size())});
                break;
            case InstrKind::Say:
                cmd.kind = CmdKind::Say;
                cmd.say.speaker_id = instr.speaker.empty() ? static_cast<u16>(0xFFFFu)
                                                             : speakers.intern(instr.speaker);
                cmd.say.text_id = push_string(&data, instr.text);
                break;
            case InstrKind::Show:
                cmd.kind           = CmdKind::Show;
                cmd.show.actor_id = actors.intern(instr.actor);
                cmd.show.pose_id  = poses.intern(instr.pose);
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
                cmd.bg.bg_id = bgs.intern(instr.bg);
                cmd.bg.fade  = instr.fade;
                break;
            case InstrKind::Wait:
                cmd.kind         = CmdKind::Wait;
                cmd.wait.seconds = instr.seconds;
                break;
            case InstrKind::Jump: {
                cmd.kind      = CmdKind::Jump;
                auto label_it = label_pcs.find(instr.name);
                // Ya validado por parse_script (etiqueta desconocida = error de
                // compilacion antes de llegar aqui); 0 es un valor seguro si de todos
                // modos faltara, sin usar excepciones (SPEC.md #4).
                cmd.jump.target_pc = label_it != label_pcs.end() ? label_it->second : 0;
                break;
            }
            case InstrKind::End:
                cmd.kind = CmdKind::End;
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

    const u32 magic            = 0x53434E56u;  // 'VNCS' (V,N,C,S en memoria little-endian)
    const u32 version          = 1;
    const u32 cmd_count        = static_cast<u32>(data.cmds.size());
    const u32 string_pool_size = static_cast<u32>(data.string_pool.size());
    const u32 label_count      = static_cast<u32>(data.labels.size());

    bool ok = true;
    ok &= std::fwrite(&magic, sizeof(magic), 1, file) == 1;
    ok &= std::fwrite(&version, sizeof(version), 1, file) == 1;
    ok &= std::fwrite(&cmd_count, sizeof(cmd_count), 1, file) == 1;
    ok &= std::fwrite(&string_pool_size, sizeof(string_pool_size), 1, file) == 1;
    ok &= std::fwrite(&label_count, sizeof(label_count), 1, file) == 1;
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

    std::fclose(file);
    return ok;
}
