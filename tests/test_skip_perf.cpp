#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "base/log.h"
#include "platform/clock.h"
#include "script/compiler.h"
#include "script/parser.h"
#include "test_config.h"
#include "vm/vm.h"

// Criterio de M7 (SPEC.md #12): "el modo skip recorre 1000 comandos en menos de 1
// segundo". vm_skip_current es exactamente lo que usa el modo skip de VnMode (game/
// vn_mode.cpp) para avanzar sin esperar input real ni temporizadores de fade/wait.
TEST_CASE("skip: 1000 comandos via vm_skip_current en menos de 1 segundo (mediana)") {
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
    CompileResult compiled = compile_instructions(parsed.instructions, "demo.vns");
    REQUIRE(compiled.ok());
    CompiledScript script{compiled.data.cmds.data(),
                           static_cast<u32>(compiled.data.cmds.size()),
                           compiled.data.string_pool.data(),
                           static_cast<u32>(compiled.data.string_pool.size())};
    REQUIRE(script.cmd_count > 0);

    constexpr u32 k_target_commands = 1000;
    constexpr u32 k_samples         = 50;
    u64           samples[k_samples];

    for (u32 s = 0; s < k_samples; ++s) {
        GameState state{};
        u64       start    = clock_now_microseconds();
        u32       executed = 0;
        // Repite el guion (185 comandos) hasta acumular 1000 comandos saltados, igual
        // que haria el modo skip si el jugador lo dejara encendido mas alla del final de
        // un guion corto de prueba.
        while (executed < k_target_commands) {
            CmdKind kind = script.cmds[state.vm.pc].kind;
            vm_skip_current(&state.vm, &state, script);
            executed += 1;
            if (kind == CmdKind::End) {
                state = GameState{};
            }
        }
        samples[s] = clock_now_microseconds() - start;
    }

    std::sort(samples, samples + k_samples);
    u64 median_us = samples[k_samples / 2];
    u64 worst_us  = samples[k_samples - 1];

    log_info("skip_perf: %u comandos, mediana=%llu us, peor caso=%llu us de %u muestras",
              k_target_commands, static_cast<unsigned long long>(median_us),
              static_cast<unsigned long long>(worst_us), k_samples);
    CHECK(median_us < 1000000);  // <1s, SPEC.md #12
}
