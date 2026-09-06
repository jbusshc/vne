#pragma once
#include <sokol_gfx.h>

#include "base/handle.h"

// Uso exclusivo de src/gfx/*.cpp: resuelve un TextureHandle a sus objetos sokol_gfx para
// construir sg_bindings. texture.h se mantiene limpio de tipos de sokol a proposito.

sg_image   texture_gpu_image(TextureHandle h);
sg_sampler texture_gpu_sampler(TextureHandle h);

// Llamadas una vez desde gfx_init/gfx_shutdown.
void texture_system_init();
void texture_system_shutdown();
