# SystemZ — instrucciones del proyecto

Motor de juegos **2D multipropósito** en C++20. Antes de escribir código lee
`docs/REDESIGN.md` completo (arquitectura objetivo, aprobada) y `docs/SPEC.md` §1–§4
(plataformas, stack cerrado, convenciones). Este archivo es el resumen operativo; en caso de
conflicto manda `REDESIGN.md`, y `SPEC.md` solo en lo que `REDESIGN.md` no sustituye.

El repositorio se sigue llamando `vne` en disco a propósito (ADR-0075): renombrarlo rompería
el entorno sin ganar nada.

## Estado actual

**Lee `docs/REDESIGN.md` antes que `docs/SPEC.md`.** El proyecto cambió de objetivo: dejó de
ser un motor de novela visual para convertirse en un **motor 2D multipropósito** llamado
**SystemZ**. `REDESIGN.md` está **aprobado** y reemplaza a `SPEC.md` §5–§11 a medida que se
implementa; `SPEC.md` §1–§4 (plataformas, stack cerrado, convenciones de código) sigue vigente.

**P0 ejecutado** (resultados en `REDESIGN.md` §5.6). **Fase activa: P1 — mundo y bloques**,
el cambio raíz: `GameState` pasa a ser un conjunto de bloques declarados por el proyecto. Plan
por fases en `REDESIGN.md` §5.3.

**Qué es SystemZ y qué no.** Un motor 2D sobre el que se construyen novelas visuales, RPGs
top-down, RPGs tácticos, plataformas y minijuegos. Esos géneros son **ejemplos, no categorías de
la arquitectura**: no hay "tipo de proyecto", ni flags por género, ni `if genre == RPG`. El
requisito duro es que **un mismo juego mezcle paradigmas** —exploración → diálogo → escena VN →
combate → minijuego— e incluso que partes distintas de una misma escena usen conceptos
distintos. El 3D de juegos completos queda **fuera** (sería otro motor, "Z2"); renderizar
elementos 3D puntuales dentro de escenas 2D sí entra, y es **lo último** del plan (P12).

**Alcance de las entregas (ADR-0076).**

| | Qué significa |
|---|---|
| **v1** | El motor es **usable** para hacer novelas visuales: alguien autora y publica una VN completa **sin escribir C++**. Por eso el proyecto-como-dato (P8) y el editor (P9) están en el camino crítico, no después. |
| **v2** | Funcional: se añade RPG top-down y los dos paradigmas se mezclan en el mismo juego y en la misma escena. |
| Después | Táctico, plataformas y minijuegos son la **demostración** de que la arquitectura los admite, no contenido a terminar. |

La arquitectura se diseña para los cinco desde el primer día aunque solo se implemente uno. Es
la única parte que no se puede posponer: un núcleo diseñado para VN y ampliado después es
exactamente el punto de partida del que este rediseño intenta salir.

## El modelo conceptual, en corto

Detalle en `REDESIGN.md` §3. Lo que hay que tener en la cabeza siempre:

> Un juego es un **mundo** de **entidades** cuyo estado vive en **bloques**; las entidades
> existen dentro de **espacios** que definen dónde pueden estar y **relojes** que definen cuándo
> avanzan; el comportamiento son **sistemas** sobre bloques en **fases**; y lo que el jugador
> puede hacer en cada momento lo decide una pila de **contextos** que activa **acciones**.

**Hay tres unidades de composición, en ejes distintos**, y el error del diseño viejo fue
meterlas en una sola cosa llamada "modo":

| Eje | Unidad | Se resuelve | Compone |
|---|---|---|---|
| Capacidad | **Feature** | al hornear el proyecto | qué sabe hacer el motor en este juego |
| Comportamiento | **Bloque de estado + sistema** | runtime, por entidad | qué reglas aplican a qué objeto |
| Interacción | **Contexto** | runtime, por momento | quién tiene el foco de input |

Más dos **portadores de restricción**, que es lo que sustituye a los flags:

- **Space** — un modelo espacial es un dato que se **referencia**, no un modo en el que se
  está. `ScreenSpace`, `TileSpace`, `WorldSpace` pueden coexistir en una escena. Una entidad se
  mueve por rejilla **porque tiene un cuerpo de rejilla**, no porque un booleano lo diga.
- **Clock** — con política `Fixed`/`Variable`/`Manual`/`Paused`. Un turno es un reloj manual;
  un diálogo sobre un combate **pausa el reloj del combate** y deja corriendo el suyo. La pausa
  parcial es un campo, no un booleano global.

Y el invariante que **se conserva** del motor viejo porque es su mejor decisión: **todo el
estado mutable del juego es POD contiguo**, guardar es un `memcpy` y el rollback una
instantánea. Lo que cambia es que su *layout* pasa a ser la concatenación de bloques que declara
el proyecto, en vez de un `struct` fijo con vocabulario de novela visual dentro.

## Reglas del rediseño

Estas tres son las que hay que defender en cada revisión. Si se rompen, el diseño se degrada
hasta volver al punto de partida.

1. **Una feature nunca modifica el núcleo.** Si hace falta tocar el núcleo para añadir una
   feature, es que al núcleo le falta un concepto: se añade el concepto y se registra el ADR, no
   el caso especial.
2. **Las features no se conocen entre sí salvo por contrato declarado** (un bloque o un tipo de
   espacio que otra publica, validado por el manifiesto). Un `if` sobre otra feature es el
   primer síntoma de degradación.
3. **Nada de vocabulario de género por debajo de `features/`.** Si aparece "actor", "diálogo" o
   "unidad" en `core/`, `world/`, `render/` o `state/`, hay una fuga de capa. Se comprueba con un
   `grep` en CI.

Y el criterio para decidir qué es núcleo: **es núcleo lo que todo juego necesita _y_ lo que las
features necesitan para interoperar.** Todo lo demás es feature. "Multipropósito" no significa
"todo en el core".

## Cambios de reglas respecto al motor viejo

Dos garantías cambiaron, **aprobadas explícitamente** por el usuario. No las revuelvas sin
volver a preguntar.

- **ADR-0077 — cero heap por frame, precisado.** Frame estacionario en cero (se sigue midiendo
  igual), más **fase de carga explícita y presupuestada** para entrar en una escena, cambiar de
  idioma o guardar. Las capacidades se declaran en el manifiesto y se validan **al hornear**,
  con nombre de escena y de bloque, en lugar de un assert tardío. Las ocho excepciones actuales
  siguen válidas y se reclasifican.
- **ADR-0078 — los opcodes ya no se validan con la exhaustividad del `switch`** (que además
  nunca funcionó bajo MSVC: `C4062` está desactivado en `/W4`). Cada feature registra un rango
  de opcodes; se valida al arrancar que no hay solapes y que todo rango tiene handler, y **el
  baker rechaza contenido con un opcode no declarado** por las features del manifiesto. Esa
  segunda mitad es la que importa y se escribe en la misma fase que el registro, no después.

También: **el texto de interfaz se localiza** (ADR-0079). Un catálogo, dos formas de clave: hash
del original para el texto de contenido (que debe invalidarse al reescribirse) y **nombre
explícito** para el de interfaz (que no debe). Cada feature declara su tabla de strings junto a
sus bloques y sus opcodes.

## Convenciones de nombre (ADR-0075)

- Producto: **SystemZ**, sin espacios.
- Macros: `SZ_ASSERT`, `SZ_DEBUG`, `SZ_EDITOR`, `SZ_SHIPPING`, `SZ_PRINTF_FMT`.
- Funciones: **sin prefijo de motor.** Se mantiene el prefijo por módulo, que ya existe y
  funciona (`arena_*`, `render_*`, `world_*`). `sz_render_draw_sprite` sería ruido.
- Objetivos de CMake: `sz_core`, `sz_platform`, `sz_rhi`, `sz_render`, `sz_runtime`,
  `sz_editor`, `sz_bake`.
- El **directorio del repositorio no se renombra**: rompería el entorno del usuario por cero
  beneficio técnico.
- Las extensiones de contenido se renombran **en la fase que cambia su formato**, no antes.
  `.vnsave` → `.szsave` en P1. El resto, cuando le toque.
- `vne` se queda en ADR-0001..0074 como historia. No se reescriben.

---

## Historia: el motor de novela visual (M0–M15, cerrado)

Lo que sigue es el registro del proyecto anterior. **Ya no describe la arquitectura objetivo**,
pero sí explica de dónde sale cada pieza, qué está verificado y con qué números, y por qué se
tomaron las decisiones que el rediseño conserva. Consúltalo como historia, no como
especificación. El detalle completo está en `docs/DECISIONS.md` (ADR-0001..0074).

**M0–M15 se cerraron todos**, con la hoja de ruta de `SPEC.md` §12 completa: esqueleto,
renderizado 2D, texto, VM y DSL, guardado/rollback, ramificación, audio, UI de novela visual,
editor, MapMode, localización, sistema de assets y `.pak`, presentación y jugabilidad,
integridad de datos, configuración, e interacción y testabilidad de la UI. 205/205 tests en Dev,
201/201 en Ship, 204/205 en Debug+ASan (solo el de rendimiento de M2, no representativo sin
optimizar, ADR-0018). Windows es la única plataforma verificada (ADR-0013).

**Lo que el rediseño conserva de ahí, y por qué** (detalle en `REDESIGN.md` §1.2): arenas con
vida declarada; handles generacionales POD y serializables; presupuestos **medidos** y no
aspiracionales (`heap_guard`); `vec3`/`mat4` desde M0, que es la única razón por la que el 3D
puntual es viable; batching por clave con radix sort en arena; render target virtual con
letterbox; el pipeline de texto completo (FreeType + HarfBuzz, atlas paginado, kinsoku,
furigana, marcado, "no se relayoutea por frame"); el `.pak` con dos backends; el hilo de IO;
el horneado offline con cero parsing en release; la tabla de símbolos de ADR-0067; la
grabación/reproducción de input de M15; y la disciplina de ADRs.

**Las tres lecciones del motor viejo que más conviene no volver a aprender:**

1. **Un identificador que se persiste no puede ser una posición ni un hash que tire el nombre**
   (ADR-0067). Había seis mecanismos para convertir un nombre en un id y todos perdían el
   nombre; se sustituyeron por una tabla donde el id es el índice, y la clase de bug
   **desapareció** en vez de quedar vigilada.
2. **Que algo no salga en el contador no prueba que no asigne: prueba que nadie ha mirado**
   (ADR-0058). `heap_guard` solo veía `operator new` y las siete librerías de terceros son C.
   Al instalarles sus hooks aparecieron cuatro infracciones que llevaban hitos ocurriendo. **Si
   añades una dependencia, instálale su hook.**
3. **La regla de cero heap solo se había verificado en frames donde el jugador no hace nada**
   (M15). Guardar, cambiar de idioma y pisar un trigger asignaban, y nadie lo había visto
   porque ninguna prueba automatizada había hecho esas cosas dentro del bucle de frame. De ahí
   sale la grabación de sesiones como instrumento de verificación, y ADR-0077.

**Dos huecos del mismo tipo, encontrados en hitos consecutivos**, que son el mejor argumento a
favor del rediseño: en M13 se descubrió que `@bg`/`@show`/`@hide` mantenían el estado
correctamente y **nadie lo convertía en sprites**; en M15, que las opciones de `@choice` no se
dibujaban en ninguna parte y un guion con ramas era **injugable** desde M5. En los dos casos el
estado estaba bien y no había nada que lo presentara, y en los dos casos ningún criterio de
aceptación lo medía. Si queda algo así, es donde hay que mirar.

**Portabilidad: se programa siempre, se verifica cuando haya máquinas.** Sigue vigente tal cual
(`SPEC.md` §2). Lo específico del SO va tras `platform/`, lo de GPU tras el RHI, y todo `#if`
lleva su rama no-Windows escrita aunque nadie la compile. Lo aplazado es **comprobar** la
portabilidad en Linux y macOS (falta hardware, no trabajo), no programarla. Nunca escribas
código solo-Windows con la excusa de que las demás plataformas son trabajo futuro.

## Reglas que no se negocian

1. **Una fase a la vez.** No empieces P(n+1) hasta que P(n) cumpla lo que `REDESIGN.md` §5.3
   dice que habilita, con la suite verde y el juego jugable. No esbozes trabajo de fases
   futuras "para adelantar".
2. **Cero dependencias nuevas.** La lista cerrada está en `docs/SPEC.md` §3. Si crees que
   falta una, **párate y pregunta**. No la añadas.
3. **Cero rediseños por iniciativa propia.** El modelo conceptual de `REDESIGN.md` §3 y la
   arquitectura de §4 son decisiones tomadas y aprobadas. Si encuentras un problema real que
   las invalida, párate, explica el problema, propón la alternativa y espera respuesta. Ojo:
   esto NO protege al código actual, que es material de partida y se puede reemplazar entero
   (ADR-0075); protege al diseño acordado.
4. **Cero asignaciones de heap en el frame estacionario.** Criterio verificable, no un ideal,
   y precisado en ADR-0077: cargar una escena o guardar es una **fase presupuestada**, no una
   excepción más. Ver skill `vne-memory-model`.
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
| `vne-serializable-state` | Al tocar el estado del mundo, guardado o rollback |
| `vne-script-dsl` | Al trabajar en el lenguaje de guion, secuencias u opcodes |
| `vne-rendering` | Al tocar `rhi/`, `render/` o `text/` |
| `vne-milestone-workflow` | Al empezar y al cerrar cualquier fase |
| `vne-build-verify` | Al configurar el build o verificar criterios |

## Estructura

```
docs/REDESIGN.md        arquitectura objetivo, aprobada. LÉELA PRIMERO
docs/SPEC.md            §1–§4 vigentes; §5–§11 los sustituye REDESIGN.md
docs/DECISIONS.md       registro de decisiones append-only (ADR-0001..)
docs/SCRIPT_LANGUAGE.md referencia del DSL de diálogo
src/                    código del motor
tools/bake/             herramientas offline de horneado
assets_src/             assets en formato de autoría
assets_baked/           generado, en .gitignore
tests/                  tests con doctest
.claude/skills/         guías por área, léelas antes de tocar la suya
```

**El árbol de `src/` está en migración.** El objetivo está en `REDESIGN.md` §4.1
(`core/ platform/ rhi/ render/ text/ audio/ resource/ loc/ world/ state/ input/ ui/ app/
features/* editor/`), con un objetivo de enlace por capa para que la regla de capas la
compruebe el enlazador. Mientras P0 no termine convive con el árbol viejo (`base/ gfx/ vm/
game/ script/ assets/`), que tiene **tres ciclos de dependencia** documentados en
`REDESIGN.md` §1.1 — no los uses como ejemplo de nada.

Dos directorios existen y están **vacíos**; se borran en P0: `shaders/` (ADR-0010 escribe los
shaders a mano por backend, viven en `src/gfx/shaders.h`) y `src/modes/`.

## Comunicación

Trabaja en español. Cuando termines una fase, entrega un resumen corto con: qué se implementó,
qué se verificó y cómo (números, no adjetivos), qué quedó pendiente, y qué decisiones nuevas se
registraron. No adornes.

Los nombres de los skills siguen empezando por `vne-` y se renombran cuando se toque cada uno:
renombrarlos todos de golpe solo cambiaría rutas sin mejorar su contenido.

Si algo no se pudo verificar (por ejemplo, no hay macOS disponible para compilar), dilo
explícitamente en vez de darlo por bueno.
