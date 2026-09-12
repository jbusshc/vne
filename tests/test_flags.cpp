#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "base/hash.h"
#include "script/compiler.h"
#include "script/parser.h"
#include "vm/state.h"
#include "vm/vm.h"

// @flag en el DSL (M13). Hasta ahora GameState.flags solo se tocaba desde Lua, asi que una
// condicion booleana obligaba a bajar a @lua — desproporcionado, y ademas suspende el
// heap_guard (ADR-0032) por una comprobacion de un bit.

namespace {

// Compila un guion y lo ejecuta entero con vm_skip_current, como hace --autoplay-script.
SymbolTable g_last_symbols;

bool run_script(const char* source, GameState* out_state, std::string* out_error) {
    ParseResult parsed = parse_script(source, "flags.vns");
    if (!parsed.ok()) {
        *out_error = parsed.errors[0].message;
        return false;
    }
    // Los ids ya no se pueden calcular con una formula desde el test (M14, ADR-0067): salen
    // de la tabla. Se guarda para poder preguntarle luego por el id de un nombre, que es
    // exactamente lo que hace el compilador.
    g_last_symbols         = symbols_for_single_script(parsed.instructions);
    CompileResult compiled = compile_instructions(parsed.instructions, "flags.vns", g_last_symbols);
    if (!compiled.ok()) {
        *out_error = compiled.errors[0].message;
        return false;
    }

    CompiledScript script{};
    script.cmds             = compiled.data.cmds.data();
    script.cmd_count        = static_cast<u32>(compiled.data.cmds.size());
    script.string_pool      = compiled.data.string_pool.c_str();
    script.string_pool_size = static_cast<u32>(compiled.data.string_pool.size());

    for (u32 steps = 0; steps < 1000; ++steps) {
        if (out_state->vm.pc >= script.cmd_count) {
            return true;
        }
        CmdKind kind = script.cmds[out_state->vm.pc].kind;
        vm_skip_current(&out_state->vm, out_state, script);
        if (kind == CmdKind::End) {
            return true;
        }
    }
    *out_error = "el guion no termino";
    return false;
}

bool flag_is_on(const GameState& s, const char* name) {
    u16 id = g_last_symbols.find(SymbolKind::Flag, name);
    return id != 0 && (s.flags[id / 8] & (1u << (id % 8))) != 0;
}

i32 var_value(const GameState& s, const char* name) {
    u16 id = g_last_symbols.find(SymbolKind::Var, name);
    return id != 0 ? s.vars[id] : 0;
}

}  // namespace

TEST_CASE("@flag: on enciende la bandera y off la apaga") {
    GameState   state{};
    std::string error;
    REQUIRE(run_script("@flag vio_carta on\n@flag tiene_llave off\n@end\n", &state, &error));
    INFO("error: " << error);
    CHECK(flag_is_on(state, "vio_carta"));
    CHECK_FALSE(flag_is_on(state, "tiene_llave"));
}

TEST_CASE("@flag: acepta true/false y 1/0 ademas de on/off") {
    GameState   state{};
    std::string error;
    REQUIRE(run_script("@flag a true\n@flag b 1\n@flag c false\n@end\n", &state, &error));
    CHECK(flag_is_on(state, "a"));
    CHECK(flag_is_on(state, "b"));
    CHECK_FALSE(flag_is_on(state, "c"));
}

TEST_CASE("@flag: un valor que no es booleano da error de compilacion") {
    GameState   state{};
    std::string error;
    CHECK_FALSE(run_script("@flag a quizas\n@end\n", &state, &error));
    CHECK(error.find("on|off") != std::string::npos);
}

TEST_CASE("@if flag: la rama se toma solo si la bandera esta encendida") {
    GameState   state{};
    std::string error;
    REQUIRE(run_script(
        "@flag vio_carta on\n"
        "@if flag vio_carta\n"
        "    @set resultado = 7\n"
        "@else\n"
        "    @set resultado = 9\n"
        "@end\n"
        "@end\n",
        &state, &error));
    INFO("error: " << error);
    CHECK(var_value(state, "resultado") == 7);
}

TEST_CASE("@if flag: con la bandera apagada se toma el @else") {
    GameState   state{};
    std::string error;
    REQUIRE(run_script(
        "@if flag nunca_puesta\n"
        "    @set resultado = 7\n"
        "@else\n"
        "    @set resultado = 9\n"
        "@end\n"
        "@end\n",
        &state, &error));
    CHECK(var_value(state, "resultado") == 9);
}

TEST_CASE("@if not flag: invierte la condicion") {
    GameState   state{};
    std::string error;
    REQUIRE(run_script(
        "@if not flag nunca_puesta\n"
        "    @set resultado = 7\n"
        "@else\n"
        "    @set resultado = 9\n"
        "@end\n"
        "@end\n",
        &state, &error));
    INFO("error: " << error);
    CHECK(var_value(state, "resultado") == 7);
}

TEST_CASE("@flag y Lua comparten el mismo bit, ahora por construccion (ADR-0067)") {
    // Antes de M14 esto dependia de que dos copias de `fnv1a % k_max_flags` —una en el
    // compilador y otra en lua_bindings.cpp— siguieran coincidiendo: si alguien cambiaba
    // una sin la otra, @flag y vn.set_flag veian banderas distintas con el mismo nombre y
    // nada lo avisaba. Ahora los dos preguntan a LA MISMA tabla, asi que no pueden divergir.
    GameState   state{};
    std::string error;
    REQUIRE(run_script("@flag compartida on\n@end\n", &state, &error));

    u16 id = g_last_symbols.find(SymbolKind::Flag, "compartida");
    REQUIRE(id != 0);
    CHECK((state.flags[id / 8] & (1u << (id % 8))) != 0);
}

TEST_CASE("@choice: una opcion condicionada por bandera se rechaza en voz alta") {
    // ChoiceOption tiene un layout fijo en el .vnc (var_id/op/rhs, ADR-0030) sin sitio para
    // una condicion de bandera. Rechazarlo es mejor que compilarlo mal en silencio.
    GameState   state{};
    std::string error;
    CHECK_FALSE(run_script(
        "@choice\n"
        "    \"opcion\" if flag x -> destino\n"
        "@end\n"
        ":: destino\n"
        "@end\n",
        &state, &error));
    MESSAGE("mensaje: " << error);
    CHECK(error.find("bandera") != std::string::npos);
}

TEST_CASE("parser: indentacion que no es multiplo de 4 se nombra como la causa (M13)") {
    // Antes esto daba "'@if' sin '@end' correspondiente", que manda a buscar el problema
    // al sitio equivocado: el @end esta ahi, lo que falla es que el cuerpo esta indentado
    // con 2 espacios y el lexer lo cuenta como nivel 0.
    const char* source =
        "@set x = 1\n"
        "@if x >= 1\n"
        "  @set y = 2\n"
        "@end\n"
        "@end\n";
    ParseResult parsed = parse_script(source, "mal_indentado.vns");
    REQUIRE_FALSE(parsed.ok());
    MESSAGE("mensaje: " << parsed.errors[0].message);
    CHECK(parsed.errors[0].message.find("indentacion") != std::string::npos);
    CHECK(parsed.errors[0].message.find("2 espacios") != std::string::npos);
    // Y apunta a la linea mal indentada, no a la cabecera del @if.
    CHECK(parsed.errors[0].line == 3);
}
