# Lenguaje de guion (.vns)

Referencia para quien escribe guiones. La especificación técnica del bytecode está en
`docs/SPEC.md` §9 y en el skill `vne-script-dsl`; esto documenta lo que el parser acepta
**hoy** (`src/script/parser.cpp`), que es lo que manda al escribir un guion de verdad.
Donde la especificación describe algo que todavía no existe, se dice aquí explícitamente.

Un `.vns` se compila a `.vnc` con `vne_bake script <entrada.vns> <salida.vnc>`. El juego
solo lee `.vnc`: nunca parsea texto en release.

## Reglas generales

- Codificación **UTF-8 obligatoria**.
- Un `#` empieza un comentario hasta el final de la línea. Un `#` dentro de comillas no
  cuenta como comentario (`"Habitacion #3"` es texto literal).
- Las líneas en blanco se ignoran.
- La indentación es de **4 espacios** y solo es significativa dentro de `@if` y `@choice`.
  El lexer divide los espacios iniciales entre 4 redondeando hacia abajo: indentar con 2
  espacios no da error ahí, simplemente cuenta como nivel 0 y el error aparece luego, al
  no encontrar el cuerpo del bloque donde se esperaba.
- Los comandos empiezan por `@`. Un comando que no esté en esta referencia es **error de
  compilación**: `comando desconocido: '@loquesea'`.
- Un salto a una etiqueta que no existe también es error de compilación, con archivo y
  línea.

## Diálogo

```
marta: Buenas noches. No esperaba visita.
"Una linea sin hablante."
```

Una línea que empieza por `identificador:` es diálogo con hablante — el identificador no
puede contener espacios. Una línea entre comillas (que abre **y cierra**) es diálogo sin
hablante. Cualquier otra línea suelta da `linea no reconocida`.

### Marcado inline

| Marca | Efecto |
|---|---|
| `{color=#rrggbb}...{/color}` | Cambia el color del tramo. Funciona. |
| `{ruby=lectura}base{/ruby}` | Furigana sobre el texto base. Funciona. |
| `{b}...{/b}` | Se parsea y el tramo queda marcado como negrita, **pero no cambia el dibujado**: no hay una fuente negrita cargada que usar. |
| `{w=n}` | Pausa de n segundos. **Se reconoce y se descarta**, sin efecto. |
| `{speed=n}` | Velocidad de la máquina de escribir. **Se reconoce y se descarta**, sin efecto. |

Las tres marcas sin efecto están anotadas en "Pendientes observados" de
`docs/DECISIONS.md`; se reconocen para que un guion escrito hoy no tenga que reescribirse
cuando se implementen.

## Etiquetas y flujo

```
:: capitulo_1          # declara una etiqueta

@jump capitulo_2       # salto incondicional
@call escena_aparte    # salta guardando la direccion de retorno (pila de 16 niveles)
@return                # vuelve al @call correspondiente
@end                   # termina el guion
```

`@end` cierra también los bloques `@if` y `@choice`; a nivel raíz es el fin del guion.

## Actores y fondos

```
@bg mansion_noche fade 0.5
@show marta neutral slot 1 fade 0.3
@hide slot 1 fade 0.3
@wait 0.5
```

`slot` va de 0 a 7. `fade` es en segundos y es opcional (por defecto 0, instantáneo).
`@show` exige actor y pose; `slot` y `fade` son pares clave-valor y su orden da igual.

Los nombres de actor, pose y fondo se internan a IDs numéricos al compilar, pero **no se
validan contra ningún registro de assets** (ADR-0022): un nombre mal escrito no da error,
simplemente se convierte en un ID distinto y en pantalla no aparece nada.

**`@move` no existe todavía.** Aparece en el ejemplo de `docs/SPEC.md` §9.1 y en el skill
`vne-script-dsl`, pero ni el parser ni `CmdKind` lo tienen (ver el comentario de cabecera
de `src/vm/cmd.h`): escribirlo da error de compilación. Lo mismo con `@transition`, que no
tiene sintaxis asignada. Para mover un actor hoy hay que ocultarlo y volver a mostrarlo en
otro slot.

## Variables y condiciones

```
@set confianza = 0     # asigna (los tres tokens y el '=' son obligatorios)
@add confianza 1       # suma (puede ser negativo: @add confianza -1)

@if confianza >= 3
    marta: Te estaba esperando.
@else
    marta: Quien eres tu?
@end
```

Las condiciones son siempre `variable OPERADOR entero`, sin expresiones compuestas ni
comparaciones entre variables. Operadores: `==`, `!=`, `<`, `<=`, `>`, `>=`.

Los valores son enteros con signo, y solo enteros: no hay literales de coma flotante ni de
cadena en `@set`/`@add`.

Las variables viven en `GameState.vars` (512 huecos), así que sobreviven a un guardado. El
nombre se resuelve a un hueco por `fnv1a % 512`, sin tabla de nombres (ADR-0029): dos
nombres distintos podrían colisionar en el mismo hueco. No hay forma de detectarlo al
compilar, así que conviene mantener el conjunto de variables pequeño y revisado.

No hay sintaxis de flags en el DSL: `GameState.flags` solo se toca desde Lua.

## Elecciones

```
@choice
    "Entrar en la casa" -> entrar_casa
    "Dar media vuelta" if valor > 2 -> marcharse
@end
```

Cada opción es `"texto" [if var OP valor] -> etiqueta`, indentada un nivel respecto al
`@choice`. Una opción con condición que no se cumple no se puede elegir. El juego espera
en el `@choice` hasta que el jugador elige; en modo skip se toma la primera opción válida.

## Audio

```
@bgm tema_tenso fade 1.0    # musica: nombre SIN extension, se resuelve por catalogo
@stopbgm fade 0.5
@sfx puerta_cierra.wav      # efecto: nombre CON extension
```

La diferencia de convención no es un descuido:

- `@bgm` guarda `fnv1a(nombre) % 65536` como `track_id`, que se resuelve en runtime contra
  el catálogo de `assets_src/ogg/` escaneado al arrancar. Tiene que ser así porque
  `GameState.bgm_track_id` es un `u16` fijo por SPEC §8.2 y debe poder restaurarse al
  cargar una partida sin el guion que inició la pista (ADR-0034).
- `@sfx` guarda la ruta literal `assets_src/ogg/<lo que escribas>` en el pool de strings
  del guion, así que el nombre debe incluir la extensión. Un efecto de sonido no sobrevive
  a un guardado, así que no necesita el rodeo del catálogo.

`@sfx` no acepta `fade`. `@bgm` y `@stopbgm` sí.

## Lua

```
@lua give_item("llave_oxidada")
```

Todo lo que sigue a `@lua` en la línea se ejecuta como una línea de Lua. La API expuesta
(`src/script/lua_bindings.cpp`, única unidad de traducción que incluye sol2) es:

```
vn.get_var(nombre) / vn.set_var(nombre, valor)
vn.get_flag(nombre) / vn.set_flag(nombre, bool)
vn.jump(etiqueta)
vn.play_sfx(nombre_con_extension)    -- misma convencion que @sfx
vn.random()                          -- avanza GameState.rng_state
```

**El estado de Lua no se serializa.** Cualquier dato que deba sobrevivir a un guardado
tiene que pasar por `vn.set_var` / `vn.set_flag`. Y nunca `math.random`: su estado no
viaja en la partida, así que un rollback daría un resultado distinto al original y el
jugador lo notaría. Usa `vn.random()`.

Ejecutar Lua asigna heap, lo que choca con la regla de cero asignaciones por frame; es una
excepción acotada y documentada (ADR-0032), no una licencia general.

## Localización

Cada línea de diálogo y cada opción de `@choice` se extraen a un catálogo con clave
`archivo:linea:hash`, donde el hash es `fnv1a` del texto original. En runtime solo cuenta
el hash: si cambias el texto original, su clave cambia y la traducción vieja deja de
aplicarse en vez de mostrarse desactualizada (ADR-0046, ADR-0047).

```
vne_bake catalog-extract catalogo_es.csv assets_src/scripts/demo.vns
vne_bake catalog-compile catalogo_en.csv assets_baked/en.vnl
```

Pese a la extensión `.csv`, el archivo intermedio **no es CSV**: son dos líneas por
entrada, la clave y luego el texto. Se eligió así porque el diálogo puede contener comas y
comillas, y evitar el escapado de CSV era más simple que implementarlo bien. Un traductor
conserva la línea de clave tal cual y traduce solo la línea siguiente.

Nota: la clave de una opción de `@choice` apunta a la línea del `@choice`, no a la de la
opción concreta, porque el parser no guarda la línea de cada opción. Solo afecta a dónde
mira el traductor, no a la estabilidad de la clave.

## Guiones de ejemplo

En `assets_src/scripts/`: `demo.vns` (diálogo lineal), `demo_branching.vns` (tres ramas,
dos finales, incluida una opción condicionada) y `demo_audio.vns` (comandos de audio).
Son placeholders de prueba, no contenido de juego.
