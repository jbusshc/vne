#include <doctest/doctest.h>

#include "game/mode.h"

namespace {

struct RecordingMode : Mode {
    u32  update_calls = 0;
    u32  render_calls = 0;
    bool block_update  = true;
    bool block_render   = false;

    void update(const InputState&, f32) override { update_calls += 1; }
    void render() override { render_calls += 1; }
    bool blocks_update_below() const override { return block_update; }
    bool blocks_render_below() const override { return block_render; }
};

}  // namespace

TEST_CASE("mode_stack: solo el modo del tope recibe update por defecto") {
    ModeStack     stack{};
    RecordingMode base;
    RecordingMode overlay;
    mode_stack_push(&stack, &base);
    mode_stack_push(&stack, &overlay);

    InputState input{};
    mode_stack_update(&stack, input, 0.016f);

    CHECK(overlay.update_calls == 1);
    CHECK(base.update_calls == 0);  // overlay.blocks_update_below() == true por defecto
}

TEST_CASE("mode_stack: blocks_update_below() == false deja pasar el update al de abajo") {
    ModeStack     stack{};
    RecordingMode base;
    RecordingMode overlay;
    overlay.block_update = false;
    mode_stack_push(&stack, &base);
    mode_stack_push(&stack, &overlay);

    InputState input{};
    mode_stack_update(&stack, input, 0.016f);

    CHECK(overlay.update_calls == 1);
    CHECK(base.update_calls == 1);
}

TEST_CASE("mode_stack: render recorre de abajo arriba salvo que algo bloquee lo de abajo") {
    ModeStack     stack{};
    RecordingMode base;
    RecordingMode overlay;  // blocks_render_below == false por defecto: base tambien se dibuja
    mode_stack_push(&stack, &base);
    mode_stack_push(&stack, &overlay);

    mode_stack_render(&stack);

    CHECK(base.render_calls == 1);
    CHECK(overlay.render_calls == 1);
}

TEST_CASE("mode_stack: blocks_render_below() == true oculta lo que hay debajo") {
    ModeStack     stack{};
    RecordingMode base;
    RecordingMode overlay;
    overlay.block_render = true;
    mode_stack_push(&stack, &base);
    mode_stack_push(&stack, &overlay);

    mode_stack_render(&stack);

    CHECK(base.render_calls == 0);
    CHECK(overlay.render_calls == 1);
}

TEST_CASE("mode_stack: on_enter/on_exit se llaman en push/pop") {
    struct EnterExitMode : Mode {
        u32 enters = 0, exits = 0;
        void on_enter() override { enters += 1; }
        void on_exit() override { exits += 1; }
        void update(const InputState&, f32) override {}
        void render() override {}
    };

    ModeStack     stack{};
    EnterExitMode m;
    mode_stack_push(&stack, &m);
    CHECK(m.enters == 1);
    CHECK(m.exits == 0);
    mode_stack_pop(&stack);
    CHECK(m.exits == 1);
    CHECK(stack.count == 0);
}
