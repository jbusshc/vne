---
name: vne-rendering
description: Capa gráfica y de texto de vne — sokol_gfx, batching de sprites por clave de ordenación, capas de render, resolución virtual con letterbox, y el sistema de texto con FreeType y HarfBuzz incluyendo word-wrap CJK, furigana y efecto de máquina de escribir. Consulta este skill SIEMPRE que vayas a tocar src/gfx o src/text, dibujar cualquier cosa en pantalla, añadir un shader, trabajar con atlas de texturas o de glifos, implementar transiciones, o resolver problemas de rendimiento de dibujado.
---

# Renderizado y texto

Objetivo permanente: **una draw call por atlas de textura por frame**. Si el contador de draw
calls sube por encima del número de atlas activos, hay un bug de ordenación.

## Capas fijas

Definidas en `gfx.h`, no se añaden capas nuevas sin registrarlas aquí:

```
0  Background
1  BackgroundOverlay
2  Actors
3  Foreground
4  DialogueBox
5  DialogueText
6  UI
7  Transition
```

## Sprite y batching

```cpp
struct Sprite {
    TextureHandle tex;
    f32 src_x, src_y, src_w, src_h;   // pixeles dentro del atlas
    f32 dst_x, dst_y, dst_w, dst_h;   // espacio virtual 1920x1080
    f32 rotation;                      // radianes, pivote en el centro
    u32 color;                         // RGBA8 premultiplicado
    u16 layer;
    u16 order;
};
```

`gfx_draw_sprite` solo acumula en un array de `g_arena_frame`. El trabajo real ocurre en
`gfx_flush`:

1. Construir una clave `u64` por sprite: `layer << 48 | order << 32 | tex.index`.
2. Ordenar con radix sort (estable, sin asignaciones, sobre la arena de frame).
3. Recorrer en orden y emitir un lote por cada tramo contiguo con la misma textura.
4. Vértices en un único buffer dinámico con `sg_append_buffer`.

No uses `std::sort` con comparador lambda aquí: el radix sort sobre `u64` es más rápido y no
asigna.

## Resolución virtual

Todo el contenido se autora en **1920x1080**, origen arriba a la izquierda, **Y hacia abajo**.

El juego renderiza a un render target fijo de 1920x1080 y se presenta escalado con letterbox
o pillarbox según el aspecto de la ventana. Ninguna lógica de juego conoce el tamaño real de
la ventana. Las únicas funciones que lo usan son `gfx_present` y la conversión de coordenadas
del ratón.

## Matemática 3D desde el principio

Usa `vec3` y `mat4` aunque `z` sea solo clave de ordenación. Migrar de `vec2` a `vec3`
después toca cada archivo del proyecto. Esto es lo que mantiene abierta la puerta al 3D.

## Shaders

SPEC.md §11 dice "GLSL fuente en `shaders/`, compilado offline por **sokol-shdc** a headers C".
**Eso no es lo que hace el proyecto hoy:** ADR-0010 decidió escribir los shaders a mano por
backend (HLSL para D3D11, GLSL para el backend GL) en vez de arrastrar el binario de
`sokol-shdc` al build, porque hay dos shaders y uno solo de ellos se compila de verdad. Están
en `src/gfx/shaders.h`.

Sigue siendo cierto lo importante: **nunca compiles shaders en runtime**. Y si el número de
shaders crece, o cuando haya que escribir MSL para Metal, la decisión se revisa: escribir MSL a
mano es justo el coste que `sokol-shdc` evita (ver SPEC.md §13.1).

## Texto

El sistema de texto es la parte más subestimada del motor. Se hace bien desde M2 porque no se
puede retrofitear.

`GlyphQuad` lleva tres campos más que el struct ilustrativo de SPEC.md §7.2 (`atlas_page`,
`color`, `is_ruby`): hacen falta para que el atlas multi-página, el marcado `{color=}` y el
furigana funcionen de verdad. Ver el comentario en `src/text/layout.h`.

```cpp
struct GlyphQuad { f32 x, y, w, h; f32 u0, v0, u1, v1; u32 atlas_page, color; u8 is_ruby; };
struct TextLayout {
    GlyphQuad* quads;
    u32        count;
    f32        width, height;
    u32        line_count;
};

TextLayout text_layout(FontHandle f, std::string_view utf8, f32 max_width, Arena* a);
void       text_draw(const TextLayout& l, f32 x, f32 y, u32 color, u32 visible_glyphs);
```

Requisitos que no son negociables:

- Caché de glifos en atlas dinámico de páginas 1024x1024, formato R8.
- Latín horneado offline; CJK rasterizado bajo demanda y cacheado.
- Word-wrap por espacios en latín y por reglas kinsoku básicas en CJK: no empezar línea con
  `。、」』）`, no terminarla con `「『（`.
- Furigana / ruby text soportado en el layout desde el diseño inicial.
- Marcado inline: `{b}`, `{color=#rrggbb}`, `{ruby=...}`, `{w=n}`, `{speed=n}`.

## La regla del máquina de escribir

**`text_layout` no se llama por frame.** Se llama una vez cuando el texto cambia. El efecto
de máquina de escribir se implementa avanzando `visible_glyphs` y dibujando solo los primeros
N quads del layout ya calculado.

Si te encuentras relayouteando cada frame, para y replantea. Es el error de rendimiento más
común en motores de VN.

## Transiciones

**No implementadas todavía: las añade M12.** La capa `Transition` (7) existe en `gfx.h` pero
nadie dibuja en ella, y `CmdKind` no tiene `Transition`. Cuando toque, se implementan como un
shader de pantalla completa sobre esa capa, con una textura de máscara y un umbral animado, de
forma que fade, wipe y disolución compartan la misma ruta de código. No escribas un sistema de
transiciones por comando.

Ojo: los `fade` que ya aceptan `@bg`, `@show` y `@hide` son interpolaciones de alfa por sprite,
que es otra cosa y ya funciona. Lo que falta es la transición de pantalla completa.

## Placeholder

Cuando un asset falla al cargar, se dibuja una textura magenta `#FF00FF` de 2x2 escalada.
Visible, inconfundible y nunca un crash.
