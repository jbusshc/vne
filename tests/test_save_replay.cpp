#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "script/compiler.h"
#include "script/parser.h"
#include "test_config.h"
#include "vm/backlog.h"
#include "vm/rollback.h"
#include "vm/save.h"
#include "vm/vm.h"

namespace {
CompiledScript load_demo_script(CompileResult* out_compiled) {
    std::string path = std::string(VNE_SOURCE_DIR) + "/assets_src/scripts/demo.vns";
    std::FILE*  f    = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string source(static_cast<usize>(size), '\0');
    REQUIRE(std::fread(source.data(), 1, static_cast<usize>(size), f) ==
            static_cast<usize>(size));
    std::fclose(f);

    ParseResult parsed = parse_script(source, "demo.vns");
    REQUIRE(parsed.ok());
    *out_compiled = compile_instructions(parsed.instructions, "demo.vns");
    REQUIRE(out_compiled->ok());

    return CompiledScript{out_compiled->data.cmds.data(),
                           static_cast<u32>(out_compiled->data.cmds.size()),
                           out_compiled->data.string_pool.data(),
                           static_cast<u32>(out_compiled->data.string_pool.size())};
}
}  // namespace

TEST_CASE(
    "test obligatorio (vne-serializable-state): guardar y recargar en cada comando da el "
    "mismo estado final que una ejecucion sin interrupciones") {
    CompileResult   compiled_baseline{};
    CompiledScript  script = load_demo_script(&compiled_baseline);

    // --- Ejecucion de referencia, sin interrupciones ---
    rollback_init(&g_rollback);
    backlog_reset(&g_backlog);
    GameState baseline{};
    bool      finished = false;
    u32       steps    = 0;
    while (!finished && steps < script.cmd_count + 1) {
        CmdKind kind = script.cmds[baseline.vm.pc].kind;
        vm_skip_current(&baseline.vm, &baseline, script);
        steps += 1;
        if (kind == CmdKind::End) finished = true;
    }
    REQUIRE(finished);
    Backlog baseline_backlog = g_backlog;

    // --- Misma ejecucion, guardando y recargando en cada comando ---
    rollback_init(&g_rollback);
    backlog_reset(&g_backlog);
    GameState   interrupted{};
    const char* save_path = "test_replay.vnsave";
    finished              = false;
    steps                 = 0;
    while (!finished && steps < script.cmd_count + 1) {
        CmdKind kind = script.cmds[interrupted.vm.pc].kind;
        vm_skip_current(&interrupted.vm, &interrupted, script);
        steps += 1;
        if (kind == CmdKind::End) {
            finished = true;
            break;
        }

        REQUIRE(save_game(save_path, interrupted, g_backlog) == SaveResult::Ok);
        GameState reloaded{};
        Backlog   reloaded_backlog{};
        REQUIRE(load_game(save_path, &reloaded, &reloaded_backlog) == LoadResult::Ok);
        interrupted = reloaded;
        g_backlog   = reloaded_backlog;
    }
    REQUIRE(finished);

    CHECK(std::memcmp(&baseline, &interrupted, sizeof(GameState)) == 0);
    CHECK(std::memcmp(&baseline_backlog, &g_backlog, sizeof(Backlog)) == 0);

    std::remove(save_path);
}
