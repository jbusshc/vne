#pragma once
#include <sokol_gfx.h>

#include "core/handle.h"

// Uso interno de src/render/*.cpp y del editor: resuelve un TextureHandle a sus objetos
// sokol_gfx para construir sg_bindings. texture.h se mantiene limpio de tipos de sokol a
// proposito.
//
// El editor es el segundo consumidor desde M15: el visor de atlas tiene que entregarle a
// ImGui un sg_image para poder dibujar la textura de verdad, y no hay forma de hacerlo sin
// el objeto de sokol. Es codigo de desarrollo que ni se compila en Ship (ver CMakeLists),
// asi que no abre esta puerta al juego distribuido.

sg_image   texture_gpu_image(TextureHandle h);
sg_sampler texture_gpu_sampler(TextureHandle h);

// Llamadas una vez desde render_init/render_shutdown.
void texture_system_init();
void texture_system_shutdown();
