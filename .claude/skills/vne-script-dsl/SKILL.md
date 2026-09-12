---
name: vne-script-dsl
description: Lenguaje de guion de vne, el conjunto de comandos como tagged union, el formato de bytecode .vnc y la integración con Lua. Consulta este skill SIEMPRE que vayas a añadir un comando nuevo al guion, tocar el lexer, el parser o el compilador del DSL, modificar la struct Cmd o el intérprete de la VM, cambiar el formato .vnc, exponer una función a Lua, o trabajar en la extracción de textos para localización.
---

# Lenguaje de guion y VM

Dos lenguajes por diseño: el DSL cubre diálogo y flujo, Lua cubre lógica compleja. No
mezcles responsabilidades.

## Sintaxis del DSL

```
# comentario de linea

:: capitulo_1                       # etiqueta

@bg mansion_noche fade 0.5
@show marta neutral slot 1 fade 0.3
@bgm tema_tenso fade 1.0

marta: Buenas noches. {w=0.4} No esperaba visita.
"Una linea sin hablante."

@wait 0.5
@sfx puerta_cierra.wav
@hide slot 1 fade 0.3

@set confianza = 0
@add confianza 1

@if confianza >= 3
    marta: Te estaba esperando.
@else
    marta: Quien eres tu?
@end

@choice
    "Entrar en la casa" -> entrar_casa
    "Dar media vuelta" if valor > 2 -> marcharse
@end

@lua give_item("llave_oxidada")
@call escena_flashback
@return
@jump capitulo_2
@end
```

Reglas: UTF-8 obligatorio, indentación de 4 espacios significativa solo dentro de `@if` y
`@choice`, `identificador:` al principio de línea es diálogo con hablante, línea entre
comillas es diálogo sin hablante, los comandos empiezan por `@`.

Marcado inline dentro del texto: `{b}`, `{color=#rrggbb}`, `{ruby=lectura}`, `{w=segundos}`,
`{speed=n}`.

**Lo que este ejemplo describe pero todavía no existe** (verificado contra
`src/script/parser.cpp`; la referencia completa y al día para escribir guiones es
`docs/SCRIPT_LANGUAGE.md`):

- `{b}` usa negrita **sintética** (`FT_GlyphSlot_Embolden` sobre la misma cara): no hay
  ningún TTF en negrita entre los assets. Necesita que el llamante pase una `bold_font` a
  `text_layout`; sin ella el tramo se dibuja normal en vez de fallar.
- `@sfx` necesita el nombre **con extensión** (se resuelve como `assets_src/ogg/<nombre>`),
  mientras que `@bgm` va **sin extensión** porque resuelve por catálogo (ADR-0034). La
  asimetría es deliberada: la pista de música tiene que sobrevivir a un guardado.
- No hay sintaxis de flags: `GameState.flags` solo se toca desde Lua hasta que M13 añada
  `@flag`.

**Un identificador desconocido es error de compilación**, con archivo y línea. Nunca un
fallo silencioso en runtime. Desde M13 eso vale de verdad también para actor, pose y fondo
(ADR-0022 cerrado, ADR-0061): se validan contra la tabla de nombres del atlas, con la
convención `actor_<actor>_<pose>` y `bg_<nombre>`. Hasta entonces la frase solo era cierta
para etiquetas, y este archivo la afirmaba igualmente.

## Comandos: tagged union

Estado real hoy (`src/vm/cmd.h`). Desde M12 el enum está completo: `Move` y `Transition`
eran los dos últimos valores de SPEC.md §8.1 que faltaban.

```cpp
enum class CmdKind : u8 {
    Nop, Say, Show, Hide, Bg, Wait, Jump, Label, End,
    SetVar, AddVar, JumpIf, Choice, ChoiceEnd, Call, Return, LuaCall,
    Sfx, Bgm, StopBgm, Move, Transition,
};

struct Cmd {
    CmdKind kind;
    u8      _pad[3];
    union {
        // key_hash: fnv1a del texto original, clave de localización (M10, ADR-0047).
        struct { u16 speaker_id; u32 text_id; u32 key_hash; }     say;
        struct { u16 actor_id; u16 pose_id; u8 slot; f32 fade; }  show;
        // ...
    };
};

static_assert(sizeof(Cmd) == 20);
static_assert(std::is_trivially_copyable_v<Cmd>);
```

Sin vtables. Sin asignación. Tamaño fijo. El guion completo es un array contiguo.

El `sizeof` son los **20** que fija SPEC.md §8.1: hasta M11 fueron 16, porque el valor lo
determinaba `Move` con sus tres `f32` y `Move` no existía. Añadirlo en M12 subió el `.vnc`
a v4 — y un `.vnc` v3 se **rechaza** con error, no se migra: el `.vnc` se regenera siempre
desde el `.vns`, a diferencia de `.vnsave`, que sí es dato de usuario persistente.

**Ojo con `-Wswitch`**: este archivo decía que los `switch` sin `default` del intérprete te
obligan a cubrir un `CmdKind` nuevo. Eso es cierto en GCC/Clang, pero **no en MSVC bajo
`/W4`** (el aviso equivalente, C4062, está desactivado). Añadir `Move`/`Transition` compiló
limpio sin tocar ninguno de los tres `switch` de `vm.cpp`. Complétalos a mano y no confíes
en que el compilador te avise aquí.

Todo campo que se serialice lleva su relleno explícito (`_pad`): el relleno implícito del
compilador no se preserva de forma fiable a través de copias bajo MSVC, lo que rompía el
`memcmp` del test obligatorio de M4 (ADR-0028). No lo omitas al añadir un `struct` a la unión.

## Cómo añadir un comando nuevo

Exactamente cuatro sitios, todos localizados:

1. Un valor en `CmdKind`.
2. Un struct anónimo en la unión de `Cmd` (verifica que no rompe el `static_assert` de
   tamaño; si lo rompe, ajusta el tamaño y documéntalo).
3. Un caso en el parser (`src/script/parser.cpp`) que reconozca la sintaxis.
4. Un caso en cada uno de los tres `switch` del intérprete: `start`, `update`, `skip_to_end`.

Los `switch` del intérprete **no tienen `default`**, para que `-Wswitch` te obligue a
completar los cuatro sitios.

## Las tres operaciones de un comando

Todo comando implementa, como funciones libres en el intérprete y no como métodos:

- `start(Cmd, GameState*)` — se ejecuta una vez al llegar al comando.
- `update(Cmd, GameState*, f32 dt) -> bool` — devuelve `true` cuando termina.
- `skip_to_end(Cmd, GameState*)` — completa el efecto instantáneamente.

`skip_to_end` no es opcional ni un extra: es lo que hace que el modo skip sea instantáneo en
vez de un parche que acelera el `dt`. Impleméntala a la vez que las otras dos, nunca después.

## Formato .vnc

```
magic 'VNCS' (4)
version (4)             v5 hoy (v2 ChoiceOption[], v3 key_hash, v4 Cmd a 20 bytes,
                        v5 tablas de nombres de actor/pose/fondo)
cmd_count (4)
string_pool_size (4)
label_count (4)
option_count (4)        ADR-0030
actor_name_count (4)    ADR-0062
pose_name_count (4)
bg_name_count (4)
Cmd[cmd_count]
string_pool             bytes UTF-8 terminados en \0, indexados por offset
Label[label_count]      { u32 name_hash; u32 pc; }
ChoiceOption[option_count]
u32[actor_name_count]   offsets en string_pool, indexados por actor_id - 1
u32[pose_name_count]    idem, por pose_id - 1
u32[bg_name_count]      idem, por bg_id - 1
```

**Los ids de actor, pose, fondo y hablante empiezan en 1, no en 0** (ADR-0062). El 0 esta
reservado para "vacio": `ActorSlot.actor_id == 0` significa slot libre (SPEC.md #8.2), y con
base 0 el primer actor de cada guion era indistinguible de un hueco. Fue un bug real que
sobrevivio hasta M13 porque nada dibujaba actores.

Las tres tablas de nombres existen porque el id es un indice de un interner **local a esa
compilacion**, no un hash: sin ellas el runtime no puede volver del id al nombre, y por tanto
no puede saber que sprite del atlas le toca. Consecuencia que conviene tener presente: un
`.vnsave` no puede restaurar `actors[]` si se carga con un guion distinto del que lo guardo.

Las opciones de un `@choice` viven en su propia tabla, no en la unión de `Cmd`, porque su
cardinalidad es variable; `Cmd::choice` guarda `first_option` y `option_count` como índices
(ADR-0030). Las etiquetas (`Label[]`) existen desde M3 pero solo se usan de verdad desde M5,
para resolver `vn.jump()` en runtime (ADR-0031).

El runtime lo carga con un solo `read` y apunta punteros a las regiones. **Cero parsing en
release.** Si te encuentras escribiendo un parser que corre en el juego, algo va mal.

## Localización

Cada texto de diálogo se extrae al catálogo con clave estable `archivo:linea:hash`. El hash
es del texto original en el idioma base. Si el texto original cambia, la clave cambia y la
traducción queda marcada como obsoleta en vez de mostrarse desactualizada.

## Lua

Confinado a `src/script/lua_bindings.cpp`, **única unidad de traducción que incluye sol2**.

API expuesta:

```
vn.get_var(name) / vn.set_var(name, value)
vn.get_flag(name) / vn.set_flag(name, bool)
vn.jump(label)
vn.play_sfx(name)
vn.random()
```

No expongas nada que dé acceso directo a punteros, handles ni al sistema de archivos. Antes
de añadir una función a esta lista, comprueba contra el skill `vne-serializable-state` que no
introduce estado fuera de `GameState`.
