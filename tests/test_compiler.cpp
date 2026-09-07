#include <doctest/doctest.h>

// Ver test_lexer.cpp: doctest necesita <ostream> completo para imprimir std::string en un
// mensaje de fallo de CHECK.
#include <ostream>

#include <cstdio>

#include "script/compiler.h"
#include "script/parser.h"

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

TEST_CASE("compiler + write_vnc: el formato binario coincide con SPEC.md #9.3") {
    ParseResult   parsed   = parse_script("personaje: Hola mundo.\n@end\n", "t.vns");
    CompileResult compiled = compile_instructions(parsed.instructions, "t.vns");
    REQUIRE(compiled.ok());

    const char* path = "test_roundtrip.vnc";
    REQUIRE(write_vnc(path, compiled.data));

    std::FILE* f = std::fopen(path, "rb");
    REQUIRE(f != nullptr);
    u32 header[5];
    REQUIRE(std::fread(header, sizeof(header), 1, f) == 1);
    CHECK(header[0] == 0x53434E56u);  // 'VNCS'
    CHECK(header[1] == 1u);
    CHECK(header[2] == static_cast<u32>(compiled.data.cmds.size()));
    CHECK(header[3] == static_cast<u32>(compiled.data.string_pool.size()));
    CHECK(header[4] == 0u);  // sin etiquetas en este guion
    std::fclose(f);
    std::remove(path);
}
