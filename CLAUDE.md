# vne — instrucciones del proyecto

Motor de novela visual en C++20. Lee `docs/SPEC.md` completo antes de escribir código.
Este archivo es el resumen operativo; la especificación manda sobre él en caso de conflicto.

## Estado actual

**Hito activo:** ninguno
**Último hito completado:** M2 — Texto. FreeType + HarfBuzz, cache de glifos en atlas
dinamico, layout con word-wrap latino y kinsoku CJK, marcado inline, furigana y efecto de
maquina de escribir. Verificado en Windows (SPEC.md §12): mediana de layout de un parrafo
de 500 caracteres ~276-285 us en build optimizada (<1ms), `text_draw` nunca relayoutea
(contador de llamadas + test), furigana geometricamente correcta, texto renderizado y
confirmado visualmente (incluye color de marcado y CJK). Se encontraron y corrigieron tres
bugs reales durante la verificacion (no solo del test): `sg_update_image` solo admite una
subida por imagen y por frame (ADR-0016, endurecido en ADR-0019 con autoproteccion en vez
de depender de disciplina), y `text_layout()` no comprobaba `nullptr` tras `arena_alloc`
(ADR-0020). 29/29 tests pasan en Ship; en Debug+ASan pasan 29/30 (el de rendimiento no es
representativo sin optimizar, ver ADR-0018), sin ningun reporte de memoria. Windows sigue
siendo la unica plataforma verificada (ADR-0013); Linux/macOS quedan abiertos
arquitectonicamente pero sin compilar/probar. Detalle completo en docs/DECISIONS.md.

M1 — Renderizado 2D (hito anterior): verificado en Windows, 5000 sprites de un atlas en 1
draw call + 1 de letterbox, >300 fps en build optimizada, letterbox correcto, cero allocs
de heap por frame, tests bajo ASan real. Backend GL escrito sin compilar (sin Linux
disponible), Metal/macOS sin implementar (ADR-0009).

Actualiza estas dos líneas al empezar y al terminar cada hito.

## Reglas que no se negocian

1. **Un hito a la vez.** No empieces M(n+1) hasta que M(n) cumpla todos sus criterios de
   aceptación en `docs/SPEC.md` §12. No esbozes trabajo de hitos futuros "para adelantar".
2. **Cero dependencias nuevas.** La lista cerrada está en `docs/SPEC.md` §3. Si crees que
   falta una, **párate y pregunta**. No la añadas.
3. **Cero rediseños por iniciativa propia.** Las secciones 4–8 de la especificación son
   decisiones tomadas. Si encuentras un problema real que las invalida, párate, explica el
   problema, propón la alternativa y espera respuesta.
4. **Cero asignaciones de heap en el bucle de frame.** Es un criterio verificable, no un
   ideal. Ver skill `vne-memory-model`.
5. **Ante ambigüedad, elige lo más simple** y regístralo en `docs/DECISIONS.md`.
6. **No inventes contenido de juego.** Si necesitas assets o guiones para probar, crea
   placeholders obvios (rectángulos de colores, texto lorem) y dilo.

## Prohibiciones de código

Sin excepciones. Sin RTTI. Sin `shared_ptr`. Sin `std::function` en rutas por frame.
Sin herencia de más de un nivel. Sin `virtual` en nada que se itere por elemento.
Sin `new`/`malloc` dentro del frame. Sin parsear texto en builds de release.

Detalle completo en el skill `vne-cpp-style`.

## Skills disponibles

Consúltalos antes de tocar el área correspondiente. Están en `.claude/skills/`.

| Skill | Cuándo |
|---|---|
| `vne-cpp-style` | Antes de escribir cualquier `.h` o `.cpp` |
| `vne-memory-model` | Al asignar memoria, crear pools o manejar recursos |
| `vne-serializable-state` | Al tocar `GameState`, guardado o rollback |
| `vne-script-dsl` | Al trabajar en el lenguaje de guion o el bytecode |
| `vne-rendering` | Al tocar `gfx/` o `text/` |
| `vne-milestone-workflow` | Al empezar y al cerrar cualquier hito |
| `vne-build-verify` | Al configurar el build o verificar criterios |

## Estructura

```
docs/SPEC.md         especificación completa (fuente de verdad)
docs/DECISIONS.md    registro de decisiones, se actualiza en cada hito
src/                 código del motor y del juego
tools/bake/          herramientas offline de horneado de assets
shaders/             GLSL fuente, compilado por sokol-shdc
assets_src/          assets en formato de autoría
assets_baked/        generado, en .gitignore
tests/               tests con doctest
```

## Comunicación

Trabaja en español. Cuando termines un hito, entrega un resumen corto con: qué se
implementó, qué criterios de aceptación se verificaron y cómo, qué quedó pendiente, y qué
decisiones nuevas se registraron. No adornes.

Si algo no se pudo verificar (por ejemplo, no hay macOS disponible para compilar), dilo
explícitamente en vez de darlo por bueno.
