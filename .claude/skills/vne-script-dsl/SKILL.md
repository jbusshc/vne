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
@sfx puerta_cierra
@move slot 1 to 0.7 0.5 in 0.4
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

**Un identificador desconocido es error de compilación**, con archivo y línea. Nunca un
fallo silencioso en runtime.

## Comandos: tagged union

```cpp
enum class CmdKind : u8 {
    Nop, Say, Show, Hide, Move, Bg, Wait, Sfx, Bgm, StopBgm,
    SetVar, AddVar, Jump, JumpIf, Choice, ChoiceEnd, Call, Return,
    LuaCall, Transition, Label, End,
};

struct Cmd {
    CmdKind kind;
    u8      _pad[3];
    union {
        struct { u16 speaker_id; u32 text_id; }                   say;
        struct { u16 actor_id; u16 pose_id; u8 slot; f32 fade; }  show;
        // ...
    };
};

static_assert(sizeof(Cmd) == 20);
static_assert(std::is_trivially_copyable_v<Cmd>);
```

Sin vtables. Sin asignación. Tamaño fijo. El guion completo es un array contiguo.

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
version (4)
cmd_count (4)
string_pool_size (4)
label_count (4)
Cmd[cmd_count]
string_pool          bytes UTF-8 terminados en \0, indexados por offset
Label[label_count]   { u32 name_hash; u32 pc; }
```

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
