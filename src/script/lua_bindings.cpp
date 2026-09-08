// Unica unidad de traduccion del proyecto que incluye sol2 (SPEC.md #9.4). El proyecto
// compila sin excepciones (SPEC.md #4); sol2 soporta ese modo si se define esto antes de
// incluir la cabecera.
#define SOL_NO_EXCEPTIONS 1
#define SOL_ALL_SAFETIES_ON 1

#include "script/lua_bindings.h"

#include <sol/sol.hpp>

#include <cstdio>

#include "audio/audio.h"
#include "base/hash.h"
#include "base/heap_guard.h"
#include "base/log.h"
#include "base/rng.h"

namespace {

sol::state g_lua;

// El codigo que se ejecuta en cada lua_run() solo tiene el nombre en texto (viene del
// pool de strings del guion), asi que las funciones vn.* resuelven variable/flag al
// mismo indice que el compilador (fnv1a % capacidad, ver docs/DECISIONS.md): no hay
// tabla de nombres en el .vnc, ni falta.
u16 var_id_of(const std::string& name) {
    return static_cast<u16>(fnv1a_u32(name) % k_max_vars);
}

u16 flag_id_of(const std::string& name) {
    return static_cast<u16>(fnv1a_u32(name) % k_max_flags);
}

bool flag_get(const GameState& state, u16 flag_id) {
    return (state.flags[flag_id / 8] & (1u << (flag_id % 8))) != 0;
}

void flag_set(GameState* state, u16 flag_id, bool value) {
    u8& byte = state->flags[flag_id / 8];
    u8  bit  = static_cast<u8>(1u << (flag_id % 8));
    byte     = value ? (byte | bit) : static_cast<u8>(byte & ~bit);
}

// Punteros validos solo durante una llamada a lua_run(): las funciones vn.* ligadas en
// lua_init() los leen en el momento en que Lua las invoca, no antes ni despues.
GameState*             g_active_state  = nullptr;
const CompiledScript*  g_active_script = nullptr;

}  // namespace

void lua_init() {
    heap_guard_suspend();
    g_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

    sol::table vn = g_lua.create_named_table("vn");

    vn.set_function("get_var", [](const std::string& name) -> i32 {
        if (g_active_state == nullptr) {
            return 0;
        }
        return g_active_state->vars[var_id_of(name)];
    });
    vn.set_function("set_var", [](const std::string& name, i32 value) {
        if (g_active_state != nullptr) {
            g_active_state->vars[var_id_of(name)] = value;
        }
    });
    vn.set_function("get_flag", [](const std::string& name) -> bool {
        if (g_active_state == nullptr) {
            return false;
        }
        return flag_get(*g_active_state, flag_id_of(name));
    });
    vn.set_function("set_flag", [](const std::string& name, bool value) {
        if (g_active_state != nullptr) {
            flag_set(g_active_state, flag_id_of(name), value);
        }
    });
    vn.set_function("jump", [](const std::string& label) {
        if (g_active_state == nullptr || g_active_script == nullptr) {
            return;
        }
        u32 target_pc = 0;
        if (vm_find_label(*g_active_script, fnv1a_u32(label), &target_pc)) {
            // Igual que Jump/Call/JumpIf (vm.cpp): vm_update suma 1 al pc despues de
            // completar el comando LuaCall que disparo esta llamada, asi que se resta 1
            // aqui para que ese +1 automatico aterrice exactamente en target_pc.
            g_active_state->vm.pc = target_pc - 1;
        } else {
            log_error("vn.jump: etiqueta desconocida '%s'", label.c_str());
        }
    });
    vn.set_function("play_sfx", [](const std::string& name) {
        // Misma convencion que el comando @sfx del DSL (script/compiler.cpp): el nombre
        // incluye la extension y se resuelve contra assets_src/ogg/, porque un efecto de
        // sonido no pasa por el catalogo por id (eso es solo para la musica, que si
        // sobrevive a un guardado, ADR-0034).
        char path[256];
        std::snprintf(path, sizeof(path), "assets_src/ogg/%s", name.c_str());
        SoundHandle handle{};
        if (audio_load(path, false, &handle) == AudioLoadResult::Ok) {
            audio_play(handle, Bus::Sfx, 1.0f, false);
        }
    });
    vn.set_function("random", []() -> u32 {
        if (g_active_state == nullptr) {
            return 0;
        }
        return rng_next(&g_active_state->rng_state);
    });

    heap_guard_resume();
}

void lua_run(const char* code, GameState* state, const CompiledScript* script) {
    g_active_state  = state;
    g_active_script = script;

    heap_guard_suspend();
    sol::protected_function_result result = g_lua.safe_script(
        code, sol::script_pass_on_error);
    heap_guard_resume();

    if (!result.valid()) {
        sol::error err = result;
        log_error("lua_run: error ejecutando @lua: %s", err.what());
    }

    g_active_state  = nullptr;
    g_active_script = nullptr;
}
