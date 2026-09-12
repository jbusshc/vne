#include <doctest/doctest.h>

// Ver test_lexer.cpp: doctest necesita <ostream> completo para imprimir std::string en un
// mensaje de fallo de CHECK.
#include <ostream>

#include <SDL3/SDL.h>

#include <cstdio>

#include "base/arena.h"
#include "base/hash.h"
#include "script/compiler.h"
#include "script/parser.h"
#include "vm/script_load.h"
#include "vm/state.h"

TEST_CASE("compiler: resuelve Jump a pc y anade End implicito si falta") {
    ParseResult parsed = parse_script(":: a\n@wait 1.0\n@jump a\n", "t.vns");
    REQUIRE(parsed.ok());
    CompileResult compiled = compile_instructions(parsed.instructions, "t.vns");
    REQUIRE(compiled.ok());

    REQUIRE(compiled.data.cmds.size() == 4);  // Label, Wait, Jump, End implicito
    CHECK(compiled.data.cmds[0].kind == CmdKind::Label);
    CHECK(compiled.data.cmds[1].kind == CmdKind::Wait);
    CHECK(compiled.data.cmds[2].kind == CmdKind::Jump);
    CHECK(compiled.data.cmds[2].jump.target_pc == 0);
    CHECK(compiled.data.cmds[3].kind == CmdKind::End);
}

TEST_CASE("compiler: no duplica End si el guion ya termina en @end") {
    ParseResult   parsed   = parse_script("@end\n", "t.vns");
    CompileResult compiled = compile_instructions(parsed.instructions, "t.vns");
    REQUIRE(compiled.data.cmds.size() == 1);
    CHECK(compiled.data.cmds[0].kind == CmdKind::End);
}

TEST_CASE("compiler: interna nombres de actor repetidos al mismo id") {
    ParseResult parsed = parse_script(
        "@show marta neutral slot 0 fade 0.0\n@show marta feliz slot 1 fade 0.0\n@end\n", "t.vns");
    CompileResult compiled = compile_instructions(parsed.instructions, "t.vns");
    REQUIRE(compiled.ok());
    CHECK(compiled.data.cmds[0].show.actor_id == compiled.data.cmds[1].show.actor_id);
    CHECK(compiled.data.cmds[0].show.pose_id != compiled.data.cmds[1].show.pose_id);
}

TEST_CASE("compiler: Sfx guarda la ruta completa en el string_pool; Bgm usa hash de catalogo") {
    ParseResult parsed = parse_script(
        "@sfx puerta_cierra.wav\n@bgm tema_a fade 0.5\n@stopbgm fade 1.0\n@end\n", "t.vns");
    REQUIRE(parsed.ok());
    CompileResult compiled = compile_instructions(parsed.instructions, "t.vns");
    REQUIRE(compiled.ok());

    REQUIRE(compiled.data.cmds.size() == 4);
    CHECK(compiled.data.cmds[0].kind == CmdKind::Sfx);
    std::string path(compiled.data.string_pool.data() + compiled.data.cmds[0].sfx.text_id);
    CHECK(path == "ogg/puerta_cierra.wav");

    CHECK(compiled.data.cmds[1].kind == CmdKind::Bgm);
    CHECK(compiled.data.cmds[1].bgm.track_id ==
          static_cast<u16>(fnv1a_u32("tema_a") % 65536u));
    CHECK(compiled.data.cmds[1].bgm.fade == doctest::Approx(0.5f));

    CHECK(compiled.data.cmds[2].kind == CmdKind::StopBgm);
    CHECK(compiled.data.cmds[2].stop_bgm.fade == doctest::Approx(1.0f));
}

TEST_CASE("compiler: Say y Choice generan key_hash y entradas de catalogo (M10)") {
    ParseResult parsed = parse_script(
        "personaje: Hola mundo.\n"
        "@choice\n"
        "    \"Opcion A\" -> fin\n"
        "@end\n"
        ":: fin\n"
        "@end\n",
        "t.vns");
    REQUIRE(parsed.ok());
    CompileResult compiled = compile_instructions(parsed.instructions, "t.vns");
    REQUIRE(compiled.ok());

    REQUIRE(compiled.data.cmds[0].kind == CmdKind::Say);
    CHECK(compiled.data.cmds[0].say.key_hash == fnv1a_u32("Hola mundo."));

    REQUIRE(compiled.data.choice_options.size() == 1);
    CHECK(compiled.data.choice_options[0].key_hash == fnv1a_u32("Opcion A"));

    REQUIRE(compiled.data.catalog_entries.size() == 2);
    CHECK(compiled.data.catalog_entries[0].text == "Hola mundo.");
    CHECK(compiled.data.catalog_entries[0].key.find("t.vns:1:") == 0);
    CHECK(compiled.data.catalog_entries[1].text == "Opcion A");
}

TEST_CASE("compiler + write_vnc: el formato binario coincide con SPEC.md #9.3") {
    ParseResult   parsed   = parse_script("personaje: Hola mundo.\n@end\n", "t.vns");
    CompileResult compiled = compile_instructions(parsed.instructions, "t.vns");
    REQUIRE(compiled.ok());

    const char* path = "test_roundtrip.vnc";
    REQUIRE(write_vnc(path, compiled.data));

    std::FILE* f = std::fopen(path, "rb");
    REQUIRE(f != nullptr);
    u32 header[6];
    REQUIRE(std::fread(header, sizeof(header), 1, f) == 1);
    CHECK(header[0] == 0x53434E56u);  // 'VNCS'
    CHECK(header[1] == 5u);  // M13: tablas de nombres de actor/pose/fondo
    CHECK(header[2] == static_cast<u32>(compiled.data.cmds.size()));
    CHECK(header[3] == static_cast<u32>(compiled.data.string_pool.size()));
    CHECK(header[4] == 0u);  // sin etiquetas en este guion
    CHECK(header[5] == 0u);  // sin choices en este guion
    std::fclose(f);
    std::remove(path);
}

TEST_CASE("script_load: un .vnc v3 obsoleto se rechaza con un error claro, no se migra") {
    // Fabricado a mano, mismo patron que test_save_load.cpp con .vnsave v1: no queda
    // ningun .vnc v3 real que generar ya con este binario (compiler.cpp escribe v5 desde
    // M13). El criterio de M12 es justamente que esto se RECHACE (docs/SPEC.md #12): un
    // .vnc nunca se migra, se regenera siempre desde el .vns fuente, a diferencia de
    // .vnsave, que si es dato de usuario persistente.
    SDL_CreateDirectory("assets_baked");  // por si el build no dejo nada horneado antes
    const char* path = "assets_baked/test_stale_v3.vnc";
    std::FILE*  f    = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    u32 header[6] = {0x53434E56u, 3u, 0u, 0u, 0u, 0u};  // 'VNCS', version 3, guion vacio
    std::fwrite(header, sizeof(u32), 6, f);
    std::fclose(f);

    // Sin pak_mount/pak_unmount aqui: test_main.cpp ya monta "." una vez para todo el
    // binario de tests, y un pak_unmount() en un solo TEST_CASE lo desmontaria para
    // cualquier otro test que corra despues (asi se rompio test_map_mode.cpp la primera
    // vez que se escribio esto).
    Arena arena = arena_create(1024, "test_stale_vnc");
    CompiledScript script{};
    CHECK(script_load("test_stale_v3.vnc", &arena, &script) == ScriptLoadResult::BadFormat);
    arena_destroy(&arena);
    std::remove(path);
}
