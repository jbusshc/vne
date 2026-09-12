# Rediseño: de motor de novela visual a motor 2D multipropósito

**Estado:** **aprobado** por el usuario el 2026-09-12. Las cinco decisiones abiertas quedaron
resueltas en ADR-0075 a ADR-0079: el motor se llama **SystemZ**, la **v1 es "usable para
novela visual"** (no solo "hace novelas visuales"), y se aprobaron explícitamente las dos
pérdidas de garantía (precisar la regla de heap, cambiar la exhaustividad del `switch` por una
tabla de opcodes validada). El texto de interfaz se resolvió con claves nombradas frente al
hash del contenido. **P0 ejecutado** (§5.6). Fase activa: **P1 — mundo y bloques**.
**Fecha:** 2026-09-12 (propuesta y aprobación)
**Alcance:** reemplaza a `SPEC.md` §5–§11 al completarse la migración. `SPEC.md` §1–§4 (objetivo,
plataformas, stack, convenciones) sobrevive casi intacto.

---

## 0. Resumen ejecutivo

El proyecto actual está **bien construido y mal dirigido** para el objetivo nuevo. La mitad
inferior (memoria, handles, plataforma, render por lotes, texto, audio, empaquetado,
herramientas offline, disciplina de verificación) es material de primera y se conserva casi
entera. La mitad superior (`vm/`, `game/`, `main.cpp`, y las partes de `gfx/` que hablan de
"actores" y "cuadro de diálogo") está construida sobre tres supuestos que el objetivo nuevo
invalida de raíz:

1. **Hay un único estado de juego, de layout fijo, conocido en tiempo de compilación.**
2. **Hay un único intérprete, y es el núcleo del motor.**
3. **El juego es el binario:** el contenido y el cableado viven en `main()`.

Ninguno de los tres se puede parchear. La propuesta los sustituye por:

1. **Estado = conjunto de bloques declarados por el proyecto** (se conserva el invariante de
   "todo el estado mutable es POD contiguo", que es la mejor decisión del proyecto, pero el
   *layout* pasa a ser composición en vez de un `struct` fijo).
2. **Comportamiento = sistemas sobre bloques, ordenados en fases, movidos por relojes.** El
   intérprete de VN pasa de núcleo a *feature*, hermano de los tilemaps y del combate.
3. **El juego = datos.** Un manifiesto de proyecto declara qué features hay, con qué
   capacidades, qué capas de render, qué acciones de input y qué escenas. El mismo manifiesto
   decide qué paneles existen en el editor.

Y añade tres conceptos que hoy no existen y sin los cuales el objetivo es inalcanzable:
**Space** (modelo espacial como dato referenciable, no como modo), **Clock** (modelo de tiempo
como dato, que hace del turno y de la pausa una propiedad y no un booleano global) y **Action**
(input como vocabulario nombrado, no como scancodes repartidos por la lógica).

La respuesta corta a "¿cuál es la unidad de composición?" es: **hay tres, en ejes distintos, y
el error del diseño actual es haberlas confundido en una sola cosa llamada "modo"**. Detalle en
§3.2.

---

## 1. Diagnóstico

### 1.1 Qué arquitectura tiene hoy el proyecto

`SPEC.md` §5 declara cinco capas estrictas (modos → vm/script → servicios → RHI → SDL) y la
regla "las capas superiores conocen a las inferiores, nunca al revés". Medido sobre el código
real, la intención es buena y **la realidad tiene tres ciclos**:

```
base      -> (nada)                                    OK
platform  -> base                                       OK
gfx       -> assets base platform                       <- gfx depende de assets
text      -> assets base gfx                            <- text depende de assets
audio     -> assets base platform                       <- audio depende de assets
assets    -> audio base gfx platform text               <- y assets depende de los tres
script    -> audio base game vm                         <- herramienta offline -> game
vm        -> assets audio base script                   <- runtime -> herramienta offline
game      -> assets audio base gfx platform text vm
editor    -> base gfx platform vm
```

- **Ciclo `assets` ↔ `{gfx, text, audio}`.** La causa es que `assets/pak.h` —resolver un
  nombre lógico a bytes— es un servicio **por debajo** de gfx/text/audio, pero vive en el
  mismo directorio que `assets/assets.h`, que está **por encima** de ellos (el hilo de IO
  integra texturas y fuentes). Son dos capas distintas metidas en una carpeta.
- **Ciclo `script` ↔ `vm`.** `vm/vm.cpp` incluye `script/lua_bindings.h`. `lua_bindings` es un
  sistema de *runtime* que está guardado en la carpeta de *herramientas offline*, y arrastra
  el ciclo. Además `script/map_bake.h` incluye `game/map_format.h`: la herramienta de horneado
  depende del código del juego.
- **Un único objetivo de enlace.** `vne_base` es una librería estática con `base + platform +
  gfx + text + audio + assets + vm + game + editor` dentro. No hay nada a nivel de build que
  impida que cualquier cosa incluya cualquier cosa; la regla de capas es honor system, y por
  eso se ha roto tres veces sin que nada avisara.

Por encima de eso, `src/main.cpp` (639 líneas) es una función-dios que contiene: creación de
ventana y GPU, carga de fuentes concretas, el texto de demostración de M2, **el banco de
pruebas de 5000 sprites de M1, que sigue dibujándose en cada frame del juego** (los logs dicen
`sprites=5158`), las instancias concretas de los cinco modos, el cableado de sus campos, el
router de teclas, el router de `ui_request`, la transición MapMode↔VnMode y el HUD. El motor y
el juego son el mismo binario, y el juego está escrito en C++ dentro de `main()`.

### 1.2 Conceptos bien definidos (se conservan)

Esto es trabajo bueno y poco frecuente. No lo toco salvo para moverlo de sitio.

| Concepto | Dónde | Por qué está bien |
|---|---|---|
| Arenas lineales con propósito y vida declarados | `base/arena` | Tres arenas, tres vidas, `reset` en vez de `free`. Modelo correcto y ya interiorizado por todo el código. |
| Handles generacionales de 8 bytes, POD, serializables | `base/handle`, `base/pool` | Convierte una clase entera de bugs en un rectángulo magenta visible. Y son serializables, que es justo lo que hace falta para que el estado sea un blob. |
| Presupuestos verificados, no aspiracionales | `base/heap_guard` | La mejor idea cultural del proyecto: una regla que se **mide**, con hooks por librería y contadores por fuente. Se generaliza, no se tira. |
| Matemática `vec3`/`mat4` desde el minuto cero | `base/math` | Decisión de M0 que hoy es la única razón por la que el 3D puntual es viable sin reescribir el render. |
| Batching por clave de ordenación + radix sort en arena | `gfx/gfx.cpp` | Una draw call por atlas. Correcto y medido (5000 sprites, 1 draw call). |
| Render target virtual + letterbox | `gfx` | La lógica de juego no conoce el tamaño de ventana. Dos funciones lo saben y están nombradas. |
| Pipeline de texto completo | `text/` | FreeType + HarfBuzz, atlas de glifos paginado, kinsoku, furigana, marcado inline, y la regla "no se relayoutea por frame". Es la parte más difícil de un motor 2D y está hecha bien. |
| `.pak` con dos backends tras una interfaz | `assets/pak` | Suelto en desarrollo, empaquetado en release, mismos nombres lógicos. Ningún cargador abre rutas literales. |
| Hilo de IO con cola fija y sin heap | `assets/assets` | Dos hilos, arenas no compartidas, un único punto de integración. Modelo correcto. |
| Horneado offline, cero parsing en release | `tools/bake` | Formato binario leído con un `read` y punteros al buffer. Es la razón por la que el runtime es barato. |
| Tabla de símbolos del proyecto | `script/symbols` | ADR-0067: se eliminó la clase de bug (ids que tiran el nombre) en vez de vigilarla. El razonamiento es reutilizable tal cual. |
| Grabación/reproducción de input | `platform/input_record` | Permite conducir el juego en la suite de tests. Es el mejor instrumento de verificación del proyecto y se generaliza. |
| Registro de decisiones append-only | `docs/DECISIONS.md` | 75 ADRs con contexto, alternativas y consecuencias. Se mantiene. |

### 1.3 Conceptos mal definidos (cambian)

**(a) `GameState` es un `struct` fijo con vocabulario de un género.**

```cpp
struct GameState {
    u32 version; VmState vm;
    ActorSlot actors[8];  u16 bg_id;              // vocabulario de VN
    i32 vars[512]; u8 flags[2048/8];              // blackboard sin tipo
    u32 rng_state;                                 // UN solo stream de azar
    u16 bgm_track_id; f32 bgm_position; f32 bus_volume[4];  // estado de un servicio
    char player_name[32]; u32 playtime_seconds;   // metadatos del guardado
    u16 map_id; f32 player_x, player_y;           // UN mapa, UN jugador
};
```

Seis problemas distintos en un solo struct:

1. `actors[8]`/`bg_id` son conceptos de novela visual **en el núcleo**. Un RPG táctico necesita
   ~40 unidades con HP, facing y estado; un plataformas necesita velocidad y `grounded`. La
   única salida dentro de este diseño es ampliar el struct con los campos de todos los géneros
   —que es el "objeto con decenas de modos" que no queremos— o variantes por género, que es el
   `if genre == RPG` disfrazado.
2. `vars[512]`/`flags[2048]` son un blackboard global sin tipo. Funciona para banderas de
   guion y es un desastre como modelo de datos de juego (nada dice qué significa `vars[37]`; el
   editor solo puede mostrar 16 enteros sin nombre).
3. `bgm_track_id`/`bgm_position`/`bus_volume` son **estado de un servicio** metido en el estado
   del juego, y encima `bus_volume` está duplicado en `config.ini`. Ya causó trabajo:
   `vm_resync_after_state_change()` existe solo para volver a empujar esto al mezclador.
4. `player_name`/`playtime_seconds` son **metadatos del archivo de guardado**, no estado
   simulado. Están dentro del bloque cuyo CRC valida la partida, así que renombrar al jugador
   toca el checksum de la simulación.
5. `rng_state` es un único stream. En cuanto dos sistemas consuman azar, el rollback desincroniza
   (retrocedes, y el orden de consumo cambia).
6. `map_id`/`player_x`/`player_y`: **un** mapa y **un** jugador, en el núcleo.

Y la consecuencia de método: cada feature nueva sube la versión del `.vnsave` global y obliga a
escribir una migración del struct entero. Van cinco versiones y cuatro migraciones encadenadas
para un motor que todavía solo hace novelas visuales.

**(b) El intérprete es el núcleo, y no debería serlo.**

`SPEC.md` §8 se llama literalmente "Núcleo: VM, estado y guardado". `vm/vm.cpp` son tres
`switch` sobre 24 valores de `CmdKind` que mutan `GameState` directamente. Añadir cualquier
comportamiento son cuatro sitios en el corazón del motor, y el propio proyecto documenta que
`-Wswitch` no protege bajo MSVC, así que la red de seguridad es disciplina manual.

El *formato* es excelente (array POD contiguo, horneado, cero parsing). El *acoplamiento* es
fatal: en un motor multipropósito, "avanzar una línea de diálogo" no puede vivir en el mismo
lugar que "avanzar el frame".

**(c) `Mode` confunde tres cosas en una.**

`Mode` tiene `update` + `render` + `blocks_update_below` + `blocks_render_below`, y en la
práctica cada modo es dueño de: su estado, su input (los scancodes están dentro), su dibujado y
su lógica. De ahí salen dos problemas medibles:

- **Duplicación de conceptos fundamentales.** `VnMode` dibuja actores y fondos; `MapMode`
  dibuja tiles y al jugador. Son dos implementaciones de "dibujar entidades posicionadas", sin
  nada compartido. Añadir un tercer paradigma añadiría una tercera.
- **Composición hardcodeada.** La pila se arma en `main()` con instancias concretas, y el
  reparto de input es `if (mode_stack.count == 1)`. Cada modo nuevo toca `main.cpp`.

`Mode` es la unidad correcta para **foco de interacción** (abrir el menú sobre el juego sin
destruirlo) y la unidad equivocada para todo lo demás. Sobrevive, muy adelgazada.

**(d) `gfx` conoce el género, y no tiene cámara.**

```cpp
enum class GfxLayer : u16 { Background, BackgroundOverlay, Actors, Foreground,
                            DialogueBox, DialogueText, UI, Transition };
```

`Actors`, `DialogueBox` y `DialogueText` son vocabulario de novela visual **dentro del
renderizador**, que es precisamente la capa que "no sabe qué es un personaje". Y el hueco
grande: **no existe ninguna cámara**. `Sprite` solo tiene `dst_x/dst_y/dst_w/dst_h` en
coordenadas de pantalla virtual; `MapMode` dibuja el mapa 1:1 desde el origen y el código
admite que "cuando haya cámara habrá que restar su desplazamiento". Sin cámara no hay RPG ni
plataformas: no hay scroll. Es el bloqueo funcional más grande del proyecto.

Además `gfx_draw_transition` es un efecto de pantalla completa **con caso especial propio** en
vez de una pasada de render, que es la forma en la que también entraría un fondo 3D.

**(e) El input no tiene vocabulario.**

27 usos directos de `SDL_SCANCODE_*` repartidos por los cinco modos, **cero soporte de
gamepad** (aunque `SPEC.md` §3 lo da por hecho), cero rebinding. Cada paradigma necesita un
vocabulario distinto (confirmar/cancelar/historial vs. 8 direcciones vs. eje analógico + salto),
y hoy eso significa más `SDL_SCANCODE` dentro de más modos.

**(f) No hay tiempo de simulación, solo `dt`.**

`SPEC.md` §6.5 dice explícitamente "sin timestep fijo", justificado en que el rollback usa
instantáneas y no reejecución. Es correcto **para una novela visual** y no lo es para un
plataformas (la física con `dt` variable no es estable) ni para un táctico (el turno es un
avance discreto, no un `dt`). Y no hay forma de pausar una parte del juego y no otra.

**(g) No hay entidades, ni animación, ni eventos.**

- La única "entidad" del motor es `ActorSlot` (8, VN). No hay identidad estable, ni jerarquía,
  ni nada que permita que el mismo objeto sea a la vez "unidad que se mueve por la rejilla",
  "hablante de un diálogo" y "portador de un inventario".
- **No existe ningún sistema de animación.** Ni animación por fotogramas, ni tweens. Los fades
  de `@show`/`@hide`/`@move` están interpolados a mano dentro de `vm.cpp`. Todos los géneros de
  la lista necesitan animación de sprites.
- **No existe ningún mecanismo de comunicación entre sistemas.** Cero colas de eventos. Hoy se
  suple con campos-bandera leídos desde `main.cpp` (`pending_trigger_script`, `wants_close`,
  `ui_request`), que funciona con 5 modos y no escala a 15 features.

**(h) El contenido no es dato.**

No hay concepto de proyecto, ni de escena. `assets_src/` está organizado **por tipo de
archivo** (`png/`, `ogg/`, `ttf/`, `maps/`, `scripts/`, `locale/`), no por contenido, así que no
hay forma de saber qué necesita una escena y por tanto no hay precarga ni streaming. El
resultado se ve en ADR-0073: cargar el guion de un trigger ocurre **dentro del frame**, y hubo
que exceptuar el guard de heap para taparlo.

### 1.4 Qué pasaría al intentar escalar el diseño actual

Ejercicio concreto: añadir **combate táctico** al proyecto tal y como está hoy.

1. `GameState` necesita `Unit units[40]` con ~10 campos cada una, más estado de rejilla, más
   cola de turnos. Sube a v6 con su migración. El `.vnsave` y las 64 instantáneas de rollback
   crecen aunque el juego sea una VN. `k_max_vars` vuelve a ser el cuello de botella para el
   estado de combate, o se duplica el blackboard.
2. `CmdKind` necesita ~15 valores nuevos (mover unidad, atacar, esperar turno, mostrar rango…)
   y tres `switch` del núcleo crecen a 40 casos. `sizeof(Cmd)` seguramente sube y el `.vnc`
   cambia de versión, invalidando todo el contenido existente.
3. `BattleMode` duplica por tercera vez el dibujado de entidades, y necesita cámara, que no
   existe: o se escribe dentro del modo (cuarta duplicación) o se añade a `gfx`.
4. `GfxLayer` necesita capas nuevas → se edita un enum del núcleo, y las capas de VN siguen
   ahí, ocupando números y significando nada.
5. El cursor de rejilla necesita input propio → más `SDL_SCANCODE` dentro del modo nuevo.
6. Un diálogo **encima** del combate necesita que el combate se pause y el diálogo no. No hay
   forma de expresarlo: `blocks_update_below` es todo o nada por modo, y el `dt` es global.
7. `main.cpp` crece con el cableado del modo nuevo y su router.

Es decir: **cada paradigma nuevo toca el núcleo en seis sitios y duplica dos conceptos
fundamentales.** Exactamente los dos resultados que el objetivo prohíbe.

---

## 2. Requisitos arquitectónicos

Derivados del diagnóstico y del objetivo. Los marcados **(implícito)** no estaban en el
enunciado y los añado porque el diseño no se sostiene sin ellos.

### 2.1 Composición

- **R1.** Un proyecto no declara género. Declara **capacidades**; el motor no tiene ninguna
  noción de "tipo de juego" en ningún sitio.
- **R2.** Varios paradigmas deben poder estar **vivos a la vez**, no solo alternarse: un
  combate en rejilla con retratos en espacio de pantalla y un diálogo encima son tres modelos
  simultáneos dentro de una escena.
- **R3.** Añadir un paradigma nuevo **no debe modificar el núcleo**. Es el criterio de
  aceptación del rediseño entero, y es falsable: ver §5, fase P9.
- **R4.** Las diferencias de comportamiento se expresan por **presencia de estado**, no por
  flags ni por ramas de género. Si una entidad se mueve por rejilla es porque tiene un cuerpo
  de rejilla, no porque un booleano lo diga.
- **R5.** Las features no dependen unas de otras salvo por **contratos declarados** (un
  bloque de estado o un tipo de espacio publicado por otra). El manifiesto valida la
  dependencia al cargar. Prohibir la dependencia forzaría a duplicar; permitirla sin declarar
  reproduce el problema actual.

### 2.2 Estado, guardado y tiempo

- **R6.** Se conserva el invariante fuerte: **todo el estado mutable del juego es POD contiguo**,
  guardar es un `memcpy` y el rollback una instantánea. Es la propiedad más valiosa del
  proyecto y la que hace posibles el rollback, los tests de replay y el editor.
- **R7.** El **layout** de ese estado es composición, no un `struct`. Añadir una feature no
  cambia el layout del estado de las demás ni obliga a migrar nada ajeno.
- **R8. (implícito)** El estado de los **servicios** (mezclador de audio, cachés, GPU) no vive
  en el estado del juego. Lo que hoy hace `vm_resync_after_state_change` se convierte en la
  regla general: los servicios se reconcilian **desde** el estado, y el estado no los contiene.
- **R9. (implícito)** Los **metadatos del guardado** (nombre, tiempo jugado, captura, fecha) son
  una sección aparte del archivo, no parte del estado simulado.
- **R10. (implícito)** El azar es por **streams nombrados** en el estado, no un `u32` global, o
  el rollback desincroniza en cuanto haya dos consumidores.
- **R11.** Debe haber varios **modelos de tiempo simultáneos**: paso fijo para simulación,
  variable para presentación, manual/discreto para turnos, y pausa **parcial** (pausar el
  combate sin pausar la UI) como propiedad, no como booleano global.
- **R12. (implícito)** Las referencias entre entidades deben **sobrevivir a guardar y cargar**.
  Los handles de runtime se derivan de ids estables guardados en el estado, no al revés.

### 2.3 Presentación e input

- **R13.** El renderizador no conoce vocabulario de género. Las capas son **claves numéricas** y
  sus nombres son dato del proyecto.
- **R14.** **Cámaras y vistas** de primera clase. Una vista es cámara + destino + filtro de
  capas; varias vistas por frame (minimapa, split-screen, fondo 3D).
- **R15.** Pasadas de render **explícitas y ordenadas**, para que un efecto de pantalla completa
  o una pasada 3D dejen de ser casos especiales.
- **R16. (implícito)** **Animación** como servicio del núcleo: fotogramas de sprite y tweens de
  propiedades, con estado serializable. Hoy no existe y todos los géneros la necesitan.
- **R17.** Input como **acciones nombradas** con bindings de teclado, ratón y gamepad,
  rebindables, y con el conjunto activo determinado por el contexto. Cero scancodes en la
  lógica de juego.
- **R18. (implícito)** La grabación de sesiones pasa a grabar **acciones**, no `InputState`
  crudo: una grabación se vuelve independiente del binding y de la resolución, y el arnés de
  tests de M15 se simplifica y se fortalece.

### 2.4 Contenido, herramientas y verificación

- **R19.** El juego es **dato**: manifiesto de proyecto + escenas + assets horneados. El binario
  del runtime no contiene contenido.
- **R20.** El editor se construye por **registro**: cada feature aporta sus inspectores,
  importadores y herramientas de autoría. Los paneles que aparecen son función de las features
  que el proyecto usa.
- **R21. (implícito)** Los bloques de estado llevan una **tabla de reflexión mínima** (nombre,
  tipo y offset por campo, declarada una vez por bloque). Sin eso el inspector del editor no
  puede ser genérico, y con eso es casi gratis.
- **R22. (implícito)** Frontera dura **editor / runtime**: el editor y el baker escriben y leen
  formatos de autoría; el runtime solo lee binario horneado. El editor es una herramienta
  aparte, no una parte del ejecutable del juego (hoy se compila dentro de `vne_game` en Dev).
- **R23. (implícito)** Grafo de **dependencias de assets por escena**, para precargar antes de
  entrar en ella. Es lo que elimina la causa de ADR-0073 en vez de exceptuarla.
- **R24.** Los presupuestos se **verifican**. Se generaliza `heap_guard`: heap por frame, tiempo
  por fase, capacidad de cada bloque, tamaño del estado. Un presupuesto que no se mide es una
  aspiración.
- **R25. (implícito)** **Degradación visible, nunca fatal**, como regla de todas las features, no
  solo de las texturas. El placeholder magenta es el precedente correcto.
- **R26. (implícito)** Portabilidad como restricción de escritura, no como tarea: se mantiene
  tal cual está hoy en `SPEC.md` §2. Lo específico del SO tras `platform/`, lo de GPU tras el
  RHI, y toda rama `#if` con su lado no-Windows escrito aunque nadie lo compile.

### 2.5 Contradicciones en los objetivos, dichas en voz alta

Cuatro, y ninguna es fatal, pero hay que resolverlas explícitamente o el diseño se rompe a
mitad de camino.

**(C1) "Cero asignaciones en el frame" vs. "motor multipropósito con contenido autorado".**
Un motor con editor no puede conocer todas las capacidades de antemano. Hoy la regla es
absoluta y ya ha necesitado **ocho excepciones documentadas**, tres de ellas descubiertas en
el último hito. Resolución propuesta: la regla se **precisa** en vez de relajarse — cero
asignaciones en el **frame estacionario**; cargar una escena es una fase explícita y
presupuestada que sí puede reservar de arenas. Y las capacidades pasan a ser **declaradas por
proyecto y validadas al cargar**, con un error claro ("este proyecto declara 40 unidades y la
escena pide 63"), que es mejor que un `VN_ASSERT` en el frame 900.

**(C2) "Sin abstracciones artificialmente genéricas" vs. "mezclar paradigmas".**
Cada mecanismo que se hace componible añade una indirección. Resolución: **la composición se
resuelve al hornear, no en el frame.** El layout de bloques, el registro de sistemas, la tabla
de capas y los opcodes se fijan al cargar el proyecto; en el frame no queda ni un `switch` de
género ni una búsqueda por nombre. La genericidad se paga en tiempo de autoría, no en runtime.

**(C3) "Extensibilidad de comandos" vs. "los `switch` sin `default` obligan a completarlos".**
La red de seguridad actual (exhaustividad del `switch`) **es incompatible** con un conjunto de
instrucciones extensible por features. Y además el proyecto ya documenta que no funciona bajo
MSVC (`C4062` desactivado en `/W4`). Resolución: se sustituye por una **tabla de opcodes
registrada y validada al arrancar** (todo rango declarado tiene handler, todo opcode del
contenido está en un rango declarado) más el rechazo del baker ante un opcode desconocido. Es
una verificación en tiempo de carga en lugar de en tiempo de compilación; hay que decirlo porque
es una pérdida real de garantía a cambio de la extensibilidad.

**(C4) "El 3D es lo último" vs. "el render debe poder alojar una pasada 3D".**
Resolución: se diseñan **las costuras ahora** (vistas, cámaras con proyección, pasadas
ordenadas, clave de ordenación con profundidad, `MeshHandle` reservado) y **no se implementa
nada 3D hasta el final**. Es exactamente lo que el proyecto hizo con `vec3`/`mat4` en M0 y hoy
es la razón por la que esto es viable. Cuesta poco ahora e es imposible después.

---

## 3. Modelo conceptual propuesto

### 3.1 La idea central

> Un juego es un **mundo** de **entidades** cuyo estado vive en **bloques**; las entidades
> existen dentro de **espacios** que definen dónde pueden estar y **relojes** que definen
> cuándo avanzan; el comportamiento son **sistemas** que operan sobre bloques en **fases**; y
> lo que el jugador puede hacer en cada momento lo decide una pila de **contextos** que activa
> **acciones** y relojes.

Ninguno de esos siete conceptos menciona un género. Todos los géneros de la lista son
combinaciones de ellos. Eso es lo que quiere decir "fundamentos suficientemente generales".

### 3.2 La unidad de composición: hay tres, y son ejes distintos

Esta es la respuesta directa a la pregunta central. El error del diseño actual es tener **una**
unidad ("modo") que carga con los tres ejes a la vez, y por eso ninguno se compone bien.

| Eje | Unidad | Cuándo se resuelve | Qué compone |
|---|---|---|---|
| **Capacidad** | **Feature** | Al hornear / cargar el proyecto | Qué sabe hacer el motor en este juego: bloques, sistemas, opcodes, importadores, paneles de editor |
| **Comportamiento** | **Bloque de estado + sistema** | En runtime, por entidad | Qué reglas se aplican a qué objeto. Sin flags: la presencia del bloque *es* la regla |
| **Interacción** | **Contexto** | En runtime, por momento | Quién tiene el foco, qué acciones están activas, qué relojes corren, qué se dibuja encima |

Y dos **portadores de restricción**, que son lo que responde a "modelos espaciales" y "modelos
de tiempo" sin flags:

| Portador | Responde a | Modelo |
|---|---|---|
| **Space** | ¿dónde puede estar algo y cómo se convierte a pantalla? | Un dato que se referencia, no un modo en el que se está |
| **Clock** | ¿cuándo avanza algo y con qué paso? | Un dato con política; pausar es una propiedad, no un booleano global |

**Por qué tres y no una.** Porque los tres ejes tienen vidas distintas. Una capacidad se
decide una vez por proyecto; un comportamiento se decide por objeto y cambia durante la
partida; un foco de interacción cambia varias veces por segundo. Meterlos en un mismo objeto
obliga a que el de vida más corta arrastre a los otros dos, que es exactamente lo que pasa hoy:
abrir un menú (interacción) instancia un objeto que también es dueño de estado (comportamiento)
y de dibujado.

### 3.3 Los conceptos, uno por uno

**Entity.** Un id estable de 8 bytes (índice + generación, el patrón de `Handle` que ya existe
y funciona). No es un objeto: es una clave. Su estado está repartido en bloques. El núcleo le
da lo mínimo: existencia, un símbolo de nombre, un espacio al que pertenece y una transformada.
Todo lo demás es de features.

**State block.** Un array denso de tamaño fijo de structs POD, más un mapa disperso
`EntityId → índice`, más una tabla de reflexión (nombre, tipo y offset de cada campo). Un
bloque es lo que una feature aporta al estado del mundo. `k_max_vars` no reaparece: cada bloque
declara su capacidad en el manifiesto, y un VN no paga por los 40 huecos de unidades tácticas.

El mundo entero es la concatenación de sus bloques en una región contigua de arena. Guardar es
escribir la tabla de bloques y volcar la región; cargar es leerla y reapuntar. **El invariante
de R6 se mantiene intacto y deja de costar una migración global por feature**: un bloque que
falta en una partida vieja simplemente se inicializa por defecto, y las migraciones pasan a ser
por bloque, no del struct entero.

**Space.** Un modelo espacial concreto con su geometría y sus restricciones. El núcleo define
la interfaz (convertir a espacio de render, consultar si una posición es válida, y qué hay
cerca) y **no** define ningún espacio concreto. Las features aportan los suyos:
`ScreenSpace` (posicionamiento libre en coordenadas virtuales, para VN y UI), `TileSpace`
(rejilla con bits de colisión), `WorldSpace` (continuo con cuerpos y gravedad). Una entidad
referencia un espacio; una escena puede tener **varios a la vez**, que es lo que hace posible
R2.

Esto es lo que sustituye a "una escena narrativa permite posicionamiento libre, una sección RPG
necesita rejilla": no son dos modos del motor, son dos espacios coexistiendo, y cada entidad
dice en cuál está.

**Clock.** Un reloj con política: `Fixed(hz)` con acumulador, `Variable`, `Manual` (avanza
cuando alguien lo tica, que es lo que necesita un turno) o `Paused`. Una escena tiene varios.
Los sistemas se registran **en un reloj**, no "en el frame". Consecuencias directas:

- La física del plataformas corre a paso fijo y es estable, sin que el diálogo tenga que hacerlo.
- Un combate por turnos es un reloj `Manual` que el sistema de turnos avanza; no hace falta
  ningún modo especial ni ningún `dt` fingido.
- Un diálogo encima del combate **pausa el reloj del combate** y deja corriendo el suyo y el de
  la UI. R11 resuelto, y la pausa parcial —que en la mayoría de los motores es una plaga de
  booleanos— es un campo de un dato.

**System.** Una función libre `void (World*, f32 dt)` registrada en una fase de un reloj. Sin
`virtual` por elemento (la regla del proyecto se respeta: la indirección es una vez por sistema
y por frame, decenas, no por entidad). Las fases son pocas, nombradas y del núcleo (entrada →
simulación → resolución → presentación → publicación), para que el orden sea explícito y no un
efecto secundario del orden de registro.

**Event.** Una cola de eventos **con vida de frame**, de tipos declarados y capacidad fija, para
que los sistemas no se llamen entre sí. Regla que la mantiene sana y que se deriva de R6: **lo
que tenga que sobrevivir a un guardado va al estado, nunca a la cola**. Una feature declara qué
eventos emite y consume, y eso es también lo que el editor puede dibujar como cableado.

**Sequence.** Lógica secuenciada, reanudable y consciente del tiempo, cuyo punto de ejecución
es estado serializable. Es la generalización honesta de lo que hoy es la VM: hace falta para
diálogos, cutscenes de RPG, eventos scriptados de un táctico y patrones de un jefe. El núcleo
aporta el *runner* (avanzar, esperar, bifurcar, llamar, guardar el punto de ejecución) y el
**registro de opcodes**; cada feature aporta su rango de opcodes. El conjunto de instrucciones
de VN pasa a ser un rango como cualquier otro.

**Context.** La pila actual, adelgazada a lo que hace bien: decir qué acciones están activas,
qué relojes corren y qué se dibuja encima. Deja de ser dueña de estado y de lógica de mundo.

**Action.** Un nombre (`confirm`, `move_axis`, `open_menu`) con bindings a teclado, ratón y
gamepad, agrupado en mapas que los contextos activan. La lógica pregunta por acciones. Efecto
lateral valioso: grabar una sesión pasa a grabar acciones (R18), y una grabación deja de
depender del binding y de la resolución.

**Feature.** Una unidad de capacidad que registra, al inicializarse: bloques (con reflexión),
sistemas (con fase y reloj), tipos de espacio, rangos de opcodes, tipos de evento,
importadores del baker y paneles de editor. Es el único punto de extensión del motor, y es el
que hace R3 comprobable: si añadir un paradigma solo escribe un directorio en `features/`, el
diseño funcionó.

### 3.4 Cómo se combinan los paradigmas: el ejemplo del enunciado

El flujo "exploración RPG → diálogo → escena VN → combate táctico → minijuego → exploración"
en este modelo:

| Tramo | Spaces | Clocks | Bloques activos | Contexto |
|---|---|---|---|---|
| Exploración | `TileSpace` del mapa | `Fixed` sim + `Variable` presentación | transform, grid body, animator, party | `explore` (mapa de acciones de 8 direcciones) |
| Diálogo | los mismos, **más** `ScreenSpace` | el del mapa **pausado**; diálogo en `Variable` | + dialogue cursor, backlog | `dialogue` apilado (acciones confirmar/saltar/historial) |
| Escena VN | `ScreenSpace` | `Variable` | + actor slots, typewriter | `dialogue` |
| Combate táctico | `TileSpace` de la batalla + `ScreenSpace` de retratos | `Manual` (turnos) + `Variable` | + unit stats, turn queue, cursor | `battle` |
| Minijuego | el que sea (`WorldSpace`) | `Fixed` propio | los de su feature | `minigame` |

Ni un flag, ni un `if genre`, y **el diálogo sobre el combate no es un caso especial**: es un
contexto apilado que pausa un reloj. El motor no sabe en ningún momento "en qué género está",
porque no es una pregunta que tenga sentido hacerle.

### 3.5 Qué es núcleo y qué es extensión

El criterio, para no acabar con todo en el núcleo: **es núcleo lo que todo juego necesita **y**
lo que las features necesitan para interoperar.** Todo lo demás es feature.

**Núcleo.** Memoria y arenas; handles y pools; ids; matemática; plataforma (ventana,
dispositivos de input, reloj, filesystem, hilos); RHI; renderizador (lotes, atlas, vistas,
cámaras, pasadas, capas numéricas); texto; audio (mezclador y buses); recursos (VFS, caché,
hilo de IO, hot reload); mundo (entidades, bloques, reflexión, espacios, relojes, fases,
sistemas, eventos); estado (guardado, rollback, migración por bloque); secuencias (runner +
registro de opcodes); input (acciones, bindings, grabación); UI (widgets, layout, foco);
localización; app (manifiesto, pila de contextos, bucle anfitrión); presupuestos.

**Extensión (features).** Diálogo/VN; tilemaps y espacio de rejilla; movimiento top-down y
triggers; física de plataformas; turnos, rangos y pathfinding; inventario y estadísticas;
partículas; timelines de cutscene; **scripting Lua**.

Dos notas sobre esa lista, porque son cambios de criterio respecto a hoy:

- **Lua deja de ser núcleo.** Hoy se compila siempre y `lua_init()` corre en todo arranque,
  incluso en un juego que no usa `@lua`. Pasa a ser una feature opcional que registra sus
  bindings. Un VN puro deja de pagar el intérprete.
- **UI sí es núcleo**, aunque hoy esté en `game/`. Todos los géneros necesitan menús, foco y
  layout, y el editor necesita lo mismo. `game/ui.h` (nine-slice + hit-testing, de M15) es una
  buena semilla; le faltan layout y foco.

### 3.6 Cómo evoluciona sin degradarse

Tres reglas estructurales que hacen que el diseño aguante. Son las que hay que defender en cada
revisión:

1. **Una feature nunca modifica el núcleo.** Si hace falta tocar el núcleo para añadir una
   feature, es que al núcleo le falta un concepto; la respuesta correcta es añadir el concepto
   (y registrarlo como ADR), no el caso especial.
2. **Las features no se conocen entre sí salvo por contrato declarado** (R5). Un `if` sobre
   otra feature es el primer síntoma de degradación.
3. **Nada de vocabulario de género por debajo de `features/`.** Si aparece la palabra "actor",
   "diálogo" o "unidad" en `render/`, `world/` o `state/`, hay una fuga de capa. Es una regla
   que se puede comprobar con un `grep` en CI, y propongo hacerlo.

---

## 4. Arquitectura

### 4.1 Módulos y dependencias

Flechas hacia abajo únicamente. Cada nivel es un **objetivo de enlace separado**, para que la
regla de capas la compruebe el enlazador y no la disciplina (esta es la corrección directa al
diagnóstico §1.1).

```
                         editor/            (herramienta aparte, NO en el runtime)
                            |
                       features/*           dialogue tilemap topdown platformer
                            |               turnbattle inventory particles lua
                            |
                          app/              manifiesto, pila de contextos, bucle anfitrión
                            |
        +-------------+-----+-----+--------------+
      state/        world/      input/         ui/
   save rollback  entidades   acciones     widgets
    migración      bloques    bindings      layout
                   espacios   grabación      foco
                    relojes
                    fases
                   sistemas
                    eventos
                  secuencias
        +-------------+-----------+-----------+
     render/        text/       audio/      loc/
      lotes        layout      mezclador   catálogo
     vistas        glifos       buses
    cámaras
    pasadas
        +-------------+-----------+
      rhi/                   resource/
    sokol_gfx              VFS (.pak) caché
    backends               hilo IO  hot reload
        +-------------+-----------+
                  platform/
          ventana  input  reloj  fs  hilos
                       |
                     core/
      arenas pools handles ids math hash rng
      crc32 log assert presupuestos sort
```

Cambios estructurales respecto a hoy, con su motivo:

| Cambio | Motivo |
|---|---|
| `base/` → `core/` | Nombre honesto; contiene los fundamentos, no una "base" de nada. |
| `assets/pak` → `vfs/`, **por debajo** de render/text/audio | Rompe el ciclo de §1.1. Resolver un nombre a bytes es un servicio inferior, no superior. |
| `assets/assets` (caché tipada + integración) **se queda arriba**, en `assets/` | Corrección hecha al ejecutar P0: si los dos vivieran en `resource/`, el ciclo seguiría existiendo dentro de la carpeta. Son dos capas distintas y necesitan dos módulos: `vfs/` solo entrega **bytes** y depende de `platform`; `assets/` los convierte en texturas, fuentes y sonidos y depende de `render`/`text`/`audio`. Es exactamente el reparto que ADR-0052 ya había decidido para el hilo de IO. |
| `gfx/` → `rhi/` + `render/` | El backend de GPU y el renderizador son dos cosas. Hoy están en la misma carpeta y `gfx_backend.h` es "de uso interno". |
| `vm/` **desaparece** | Se reparte: estado → `state/` + `world/`, runner → `world/sequence`, comandos de VN → `features/dialogue`. |
| `script/` **desaparece** | Compilador y baker → `tools/bake` (con importadores por feature); `lua_bindings` → `features/lua`; `symbols` → `core` + baker. Elimina los dos ciclos restantes. |
| `game/` **desaparece** | Modos → contextos en `app/` + features; `ui` → `ui/` del núcleo; `config`/`locales` → manifiesto y `loc/`. |
| `main.cpp` → `app/host.cpp` | ~50 líneas: montar VFS, cargar manifiesto, inicializar features, correr. |
| `loc/` nuevo | La localización es un servicio del núcleo porque **toda** feature emite texto; y cierra el pendiente de M15 (el texto de interfaz no se localiza). |
| `editor/` fuera del runtime | R22. Hoy se compila dentro de `vne_game` en Dev. Pasa a binario propio que carga el mismo proyecto. |

### 4.2 Interfaces principales

Boceto para fijar la forma, no la firma final.

**Mundo y bloques**

```cpp
struct EntityId { u32 index; u32 gen; };            // mismo patrón que Handle

// Una feature declara un bloque una vez. La reflexión es una tabla estática:
// sin RTTI, sin macros mágicas, y suficiente para que el inspector sea genérico (R21).
struct BlockField { const char* name; FieldType type; u16 offset; u16 count; };
struct BlockDesc {
    const char*       name;          // "grid_body", "unit_stats"
    u32               stride;        // sizeof del struct del bloque
    u32               capacity;      // del manifiesto, no una constante del motor
    const BlockField* fields;
    u32               field_count;
    u8                rollback : 1;  // entra en las instantáneas
    u8                persist  : 1;  // entra en el guardado
};

BlockId  world_register_block(World*, const BlockDesc&);
void*    world_block_add(World*, BlockId, EntityId);   // nullptr si lleno (error claro)
void*    world_block_get(World*, BlockId, EntityId);   // nullptr si no tiene
Span<T>  world_block_dense(World*, BlockId);           // iteración densa para sistemas
```

`rollback` y `persist` por separado importan: un tilemap se persiste y no necesita entrar en 64
instantáneas; una interpolación visual no necesita ni una cosa ni la otra. Hoy todo entra en
todo porque es un solo struct.

**Espacios y relojes**

```cpp
struct SpaceVTable {                 // una indirección por espacio, no por entidad
    Vec3 (*to_render)(const Space*, Vec3 local);
    bool (*is_valid)(const Space*, Vec3 local, Vec3 extent);
    u32  (*query)(const Space*, Aabb, EntityId* out, u32 cap);
};
SpaceId world_create_space(World*, SpaceTypeId, const void* params);

enum class ClockPolicy : u8 { Fixed, Variable, Manual, Paused };
ClockId world_create_clock(World*, ClockPolicy, f32 hz_or_zero);
void    world_clock_set_policy(World*, ClockId, ClockPolicy);   // pausa parcial = esto
u32     world_clock_tick(World*, ClockId, u32 steps);           // turnos
```

**Sistemas y fases**

```cpp
enum class Phase : u8 { Input, Simulate, Resolve, Present, Publish };
using SystemFn = void (*)(World*, f32 dt);
void world_register_system(World*, Phase, ClockId, SystemFn, const char* name);
```

`name` no es decoración: es lo que el presupuesto de tiempo por fase (R24) y el editor
necesitan para poder decir *qué* se pasó del presupuesto.

**Render**

```cpp
struct Camera { Vec3 position; f32 zoom; f32 rotation; Projection proj; };
struct View   { Camera camera; RenderTarget target; u16 layer_min, layer_max; };

void render_begin_frame();
void render_push_view(const View&);      // varias vistas por frame (R14)
void render_sprite(const Sprite&);       // dst en coordenadas de ESPACIO, no de pantalla
void render_push_pass(const PassDesc&);  // 3D opaco, lotes 2D, post, UI (R15)
void render_end_frame();
```

`Sprite::layer` sigue siendo un `u16`; la tabla de nombres de capa es dato del proyecto (R13).
El `GfxLayer` con `Actors`/`DialogueBox` desaparece del núcleo.

**Secuencias y opcodes**

```cpp
struct SeqCursor { u32 program; u32 pc; u8 phase; f32 timer; u32 call_stack[8]; u8 depth; };
// ^ estado serializable: es lo que hoy es VmState, pero puede haber N a la vez
//   (un diálogo, una cutscene y el patrón de un jefe corriendo en paralelo)

using OpcodeFn = SeqResult (*)(World*, SeqCursor*, const Instruction&, f32 dt);
void sequence_register_range(u16 first, u16 count, OpcodeFn);   // reemplaza al switch (C3)
```

**Input**

```cpp
ActionId input_action(const char* name);
bool     input_pressed(ActionId);   // flanco
bool     input_held(ActionId);
f32      input_axis(ActionId);      // teclas, stick o ratón, ya normalizado
void     input_push_map(ActionMapId);   // lo hace el contexto, no la lógica
```

**Feature**

```cpp
struct FeatureDesc {
    const char* name;
    u32         version;
    const char* const* requires;  u32 requires_count;   // contratos declarados (R5)
    void (*register_all)(Engine*);                       // bloques, sistemas, espacios,
                                                         // opcodes, eventos, importadores
#if defined(Z_EDITOR)
    void (*register_editor)(EditorRegistry*);            // paneles e inspectores (R20)
#endif
};
```

### 4.3 El manifiesto de proyecto

Un archivo de autoría (texto, editable a mano y por el editor) horneado a binario. Es la
única cosa que el runtime necesita para saber qué juego es.

```
features:  dialogue tilemap topdown inventory
capacities: entities=512 grid_body=256 unit_stats=0 dialogue_cursor=4 ...
layers:    background=0 terrain=10 actors=20 overlay=30 ui=40 transition=50
actions:   confirm=[key:Space,key:Return,pad:A,mouse:Left] move=[axis:WASD,pad:LeftStick] ...
scenes:    pueblo interior_casa combate_puente
entry:     pueblo
locales:   es ja
```

Propiedades que salen gratis de tener esto:

- El estado del mundo se dimensiona por proyecto. `unit_stats=0` significa que el bloque no
  existe y no ocupa un byte: **es la desaparición definitiva de `k_max_vars`** como problema.
- El editor sabe qué paneles mostrar sin preguntar a nadie (R20): los de las features listadas.
- El validador puede rechazar el proyecto al hornear con un error claro en vez de reventar en
  el frame 900 (C1).
- El binario del runtime deja de contener contenido (R19), y `main()` deja de ser el juego.

### 4.4 Guardado y rollback, con el invariante intacto

```
.zsave
  header   magic, versión de formato, hash del manifiesto
  meta     nombre, tiempo jugado, fecha, miniatura        <- fuera del estado (R9)
  blocks   tabla: [nombre, stride, count, offset, crc]    <- por bloque, no global
  data     región contigua de los bloques con persist=1
```

- Guardar sigue siendo volcar una región contigua. **R6 intacto.**
- Añadir una feature **no** cambia el layout de los bloques ajenos: la tabla es por nombre.
- Un bloque que falta en una partida vieja se inicializa por defecto. Un bloque cuyo `stride`
  cambió necesita una migración **de ese bloque**, escrita junto a la feature, no una migración
  del estado entero en el núcleo. Adiós a la cadena v1→v2→v3→v4→v5 y a las cuatro funciones
  encadenadas.
- El rollback solo copia los bloques con `rollback=1`, y su coste pasa a ser proporcional a lo
  que de verdad cambia.
- Los ids de entidad son estables y se guardan; los handles de runtime se derivan al cargar
  (R12).

### 4.5 El editor, derivado del runtime

El editor es un binario aparte que carga el mismo manifiesto y las mismas features, con la
librería del runtime enlazada. Su núcleo es un **shell + registro**; no conoce ninguna feature.

- **Inspector genérico** sobre `BlockDesc::fields`: cualquier bloque de cualquier feature es
  inspeccionable y editable sin escribir un panel. Hoy el inspector muestra `vars[0..15]` sin
  nombre porque no hay nada que consultar.
- **Paneles por feature** (R20): el editor de tilemaps aparece si el proyecto usa `tilemap`. El
  editor de diálogo, si usa `dialogue`. Nada obliga a que todo proyecto tenga las mismas
  herramientas, que es literalmente lo que pediste.
- **Play-in-editor** es posible porque el mundo es dato: correr los sistemas sobre el mundo
  autorado es lo mismo que hace el runtime.
- **El visor de atlas, el inspector de estado y el grafo de fases/presupuestos** son paneles del
  shell, no de features.
- El baker comparte el registro de importadores con el editor: una feature declara "yo importo
  `.tmx`" en un solo sitio.

### 4.6 Presupuestos, generalizando `heap_guard`

`heap_guard` es la mejor idea cultural del proyecto y hoy solo mide una cosa. Se convierte en un
módulo `core/budget` con el mismo espíritu —**medido, no aspiracional**— sobre cuatro ejes:

| Presupuesto | Se comprueba | Qué previene |
|---|---|---|
| Heap por frame (existente) | al final del frame | la regresión que ya se ha detectado tres veces |
| Tiempo por fase y por sistema | al cerrar cada fase | que un sistema se coma el frame sin que se sepa cuál |
| Ocupación de cada bloque | al añadir | el `VN_ASSERT` tardío; da error con nombre de bloque y de escena |
| Tamaño total del estado | al cargar el proyecto | que el rollback de 64 instantáneas crezca sin que nadie lo note |

Y la regla de la excepción se mantiene como está hoy (acotada, documentada con ADR, con la
tabla en el skill), porque ha funcionado: las ocho excepciones actuales están todas
justificadas y nombradas.

### 4.7 La costura 3D (último paso, diseñada ya)

Lo que hay que dejar preparado y **no** implementar hasta el final:

- `View` con `Projection` que admita perspectiva además de ortográfica (§4.2 ya la lleva).
- Pasadas explícitas, para que "3D opaco → sprites 2D → post → UI" sea una lista y no un caso
  especial (R15).
- Clave de ordenación con sitio para profundidad, y depth buffer opcional por pasada.
- `MeshHandle` reservado en el sistema de handles y un hueco para un formato de malla horneado.

Con eso, "un fondo 3D o un modelo dentro de una escena de novela visual" es **una feature**
(`features/mesh3d`) que registra una pasada y un bloque, sin tocar el núcleo. Y si algún día se
convierte en un motor 3D de verdad, ese es el Z2 y no este: la costura permite el caso
puntual, no pretende sostener un juego 3D entero.

---

## 5. Plan de migración

### 5.1 Principios del plan

1. **La suite de tests queda verde al final de cada fase**, y el juego de demostración
   jugable. No hay fase "el motor está roto mientras".
2. **Cada fase entrega una capacidad, no una refactorización abstracta.** Si una fase no se
   puede justificar por lo que habilita, está mal planteada.
3. **El orden lo dicta la dependencia, no el riesgo.** Lo más profundo primero (estado), porque
   todo cuelga de ahí.
4. **P9 es la falsación del diseño.** Si añadir un plataformas y un táctico toca el núcleo, el
   rediseño no funcionó y hay que volver a §3.

### 5.2 El nombre: decidido

**SystemZ** (ADR-0075). Sin espacios, la `Z` como última letra. Reglas fijadas para no volver
a decidirlo: macros `SZ_*`; funciones **sin** prefijo de motor (se mantiene el prefijo por
módulo, que ya existe y funciona: `arena_*`, `render_*`, `world_*`); objetivos de CMake `sz_*`;
el directorio del repositorio **no** se renombra (rompería el entorno del usuario por cero
beneficio); las extensiones de contenido se renombran en la fase que cambia su formato, no
antes; y `vne` se queda en los ADR-0001..0074 como historia.

### 5.3 Fases

Reordenadas según ADR-0076: **la v1 no es "el motor hace novelas visuales", es "el motor es
usable para hacer novelas visuales"**. Eso sube dos fases que antes eran tardías —el proyecto
como dato y el editor— a requisitos de la v1, porque son exactamente lo que separa las dos
frases. Cada fase deja la suite verde y el juego jugable.

#### Camino a la v1 — usable para novela visual

**P0 — Saneamiento estructural y rename.** *Sin cambios de comportamiento.*
Rename a SystemZ (macros, objetivos, docs). Partir `vne_base` en objetivos de enlace por capa,
para que la regla de capas la compruebe el enlazador y no la disciplina. Romper los tres
ciclos: `pak` baja a `resource/`, `lua_bindings` sale de `script/`, `map_bake` deja de incluir
`game/`. Mover `base/`→`core/`, partir `gfx/`→`rhi/`+`render/`. **Borrar el banco de pruebas de
5000 sprites del bucle de frame** (moviendo el criterio de M1 a un test, que es donde debía
estar) y el contenido de demostración de `main()`. Añadir al CI el `grep` que prohíbe
vocabulario de género por debajo de `features/`.
*Habilita:* que todo lo demás sea revisable en diffs pequeños. *Riesgo:* bajo, es mecánico.
*Los tests no deben cambiar ni una línea.*

**P1 — Mundo y bloques.** *El cambio raíz.*
`EntityId`, bloques con reflexión estática, registro, capacidades desde un manifiesto mínimo.
Portar el `GameState` actual a bloques. Guardado y rollback por bloques (`.vnsave` → `.szsave`,
con importador del v5 actual para no perder los fixtures). Sacar del estado del juego lo que no
es estado de juego: volúmenes y posición de música (R8) y metadatos del guardado (R9). Streams
de RNG nombrados (R10).
*Habilita:* que dos features tengan estado sin pisarse, y que el editor pueda inspeccionar
cualquier cosa. *Riesgo:* **el más alto del plan.** La red es el test de replay de M4 (guardar y
recargar en cada uno de los 185 comandos): si pasa, la migración es correcta.

**P2 — Relojes, fases y sistemas.**
Registro de sistemas, las cinco fases, relojes con las cuatro políticas. El cuerpo de `main()`
se convierte en sistemas registrados y `app/host.cpp` queda en ~50 líneas. Presupuesto de
tiempo por fase y por sistema (R24), y la fase de carga presupuestada de ADR-0077.
*Habilita:* paso fijo, pausa parcial y avance discreto. Una VN no los necesita; sin ellos, tres
de los cinco paradigmas no son expresables, y este es el momento barato de tenerlos.

**P3 — Acciones e input.**
Mapas de acciones como dato, bindings de teclado, ratón y **gamepad**, rebinding. Fuera los 27
`SDL_SCANCODE` de la lógica de juego. Regrabar la sesión de M15 en acciones (R18): la grabación
se vuelve más corta, más legible e independiente del binding.
*Habilita:* gamepad, que hoy no existe pese a que `SPEC.md` §3 lo daba por hecho.

**P4 — Render: capas como dato, vistas, cámaras y pasadas.**
Tabla de capas del proyecto en lugar del `enum` con `Actors`/`DialogueBox`. `Camera`/`View`,
sprites en coordenadas de espacio, pasadas explícitas, y `gfx_draw_transition` convertido en
pasada en vez de caso especial. Se conservan el batching, el radix sort, el atlas, el letterbox
y los dos backends.
*Habilita:* saca el vocabulario de género del renderizador (que es lo que la v1 necesita) y deja
el scroll hecho para la v2. Se hace entero aquí porque partirlo en dos significaría tocar el
mismo código dos veces.

**P5 — Secuencias, y el diálogo como feature.**
Runner de secuencias con estado serializable + registro de rangos de opcodes (ADR-0078). Los 24
comandos de VN se mudan a `features/dialogue` con su rango, su backlog y sus opciones.
`features/lua` pasa a ser opcional: un VN que no usa `@lua` deja de pagar el intérprete.
Secuencias en paralelo.
*Habilita:* que el conjunto de instrucciones crezca sin tocar el núcleo, y cutscenes en
cualquier paradigma. **Es el corazón de la VN, y a partir de aquí el núcleo ya no sabe qué es un
diálogo.**

**P6 — Espacios (solo `ScreenSpace`).**
Interfaz de `Space` en el núcleo y `features/screenspace` para el posicionamiento libre que usan
la VN y la UI. `TileSpace` **no** se implementa aquí: llega con la v2. Se verifica que dos
espacios pueden coexistir en una escena aunque en v1 solo haya uno en uso.
*Habilita:* R4 de verdad (la regla se aplica por presencia de estado, no por flag).

**P7 — Animación y UI.**
`animator` con fotogramas de sprite y tweens de propiedades, con estado serializable (R16): los
fades de `@show`/`@hide`/`@move` interpolados a mano dentro de `vm.cpp` pasan a ser tweens.
`ui/` del núcleo con layout y foco, partiendo del nine-slice de M15.
*Habilita:* que ninguna feature reimplemente animación, y menús decentes sin pelear con
coordenadas a mano.

**PX — Sonda arquitectónica.** *Corta y no negociable.*
`features/minigame`: un `WorldSpace`, un reloj de paso fijo, dos entidades y un contador, bajo
la restricción explícita de **no modificar ni una línea del núcleo**. Si hay que tocarlo, al
núcleo le falta un concepto y se corrige **aquí**, no en la v2.
*Habilita:* falsar el diseño meses antes de que lo haría el segundo paradigma real. Es la
mitigación del riesgo que introduce ADR-0076 al implementar un solo paradigma hasta la v1.

**P8 — El proyecto como dato.**
Manifiesto completo, escenas como dato, grafo de dependencias de assets, precarga por escena
(elimina la causa de ADR-0073 en vez de exceptuarla), `assets_src/` reorganizado por contenido y
no por tipo de archivo. Localización del texto de interfaz según ADR-0079.
*Habilita:* que el juego deje de vivir en el binario. Es el prerrequisito del editor **y** la
mitad de lo que significa "usable".

**P9 — Editor de autoría.**
Shell + registro, inspector genérico derivado de la reflexión de bloques, paneles por feature,
play-in-editor, y lo que hace falta para trabajar en una VN a diario: edición de guiones con
recarga en caliente, previsualización de escena, visor de atlas, inspector de estado.
*Habilita:* **la otra mitad de "usable".** Aquí se cierra la v1.

> **v1 — el motor es usable para hacer novelas visuales.** Alguien autora y publica una VN
> completa sin escribir C++.

#### Camino a la v2 — funcional

**P10 — RPG top-down.**
`features/tilemap` con `TileSpace` y bits de colisión, `features/topdown` con movimiento y
triggers, cámara que sigue al jugador, y el editor de mapas. Primera prueba con un paradigma
real de que añadir uno no toca el núcleo.

> **v2 — funcional: VN + RPG top-down**, y los dos mezclables en el mismo juego y en la misma
> escena, que es el requisito duro del objetivo.

#### Después

**P11 — Demostración de los paradigmas restantes.** `features/platformer` (cuerpos, gravedad,
colisión continua, `WorldSpace`) y `features/turnbattle` (cola de turnos, rangos, pathfinding,
`TileSpace` compartido con `tilemap` por contrato declarado). **Criterio de aceptación: cero
líneas modificadas en `core/`, `world/`, `render/`, `state/` o `app/`.** Son la demostración de
que la arquitectura los admite, no contenido a terminar (ADR-0076).

**P12 — Costura 3D.** `MeshHandle`, formato de malla horneado, pasada 3D con depth, cámara en
perspectiva. `features/mesh3d` usable como fondo o modelo **dentro** de una escena 2D. Y aquí
se decide, con datos en la mano, si tiene sentido plantear el Z2.

### 5.4 Qué se conserva, refactoriza, reemplaza o borra

**Se conserva casi tal cual** (se mueve de carpeta, no se reescribe):
`arena`, `pool`, `handle`, `math`, `hash`, `crc32`, `rng`, `radix_sort`, `log`, `assert`,
`heap_guard` + hooks; `platform/{window, clock, files, input}`; `text/` **completo** (FreeType,
HarfBuzz, glyph cache, layout, kinsoku, furigana, marcado); el batching y el empaquetador shelf
de atlas; los dos backends de GPU; `resource` (`pak` + hilo de IO + hot reload); el baker
(TMX, atlas, subconjunto de fuentes, catálogos, símbolos); **toda la suite de tests**; el arnés
de grabación/reproducción; y `DECISIONS.md`.

**Se refactoriza** (la idea es buena, la interfaz cambia):
API pública de `render` (cámaras, vistas, capas, pasadas); `resource` partido en VFS y caché;
`input` con capa de acciones; `Mode` → `Context` adelgazado; guardado y rollback por bloques;
`editor` a shell + registro; `game/ui.h` a `ui/` con layout y foco.

**Se reemplaza** (el concepto estaba mal):
`vm/state.h` (`GameState`) → mundo + bloques; los tres `switch` de `vm.cpp` → runner +
opcodes registrados; `main.cpp` → `app/host.cpp` + manifiesto; los cinco `*_mode.cpp` →
contextos + features; `GfxLayer` → tabla de capas del proyecto; `locales.h` y `config.cpp` →
manifiesto y `loc/`.

**Se borra:**
el banco de 5000 sprites del bucle de frame (debris de M1 que sigue corriendo); el contenido de
demostración dentro de `main()`; `script/hash_collisions` (existe porque los ids eran hashes; la
tabla de símbolos de ADR-0067 ya lo hizo innecesario para variables y banderas — se termina el
trabajo con pistas y mapas y se borra); `map_catalog` como caso especial (pasa al catálogo
genérico de recursos); los directorios vacíos `src/modes/` y `shaders/`.

**Falta implementar por completo** (no existe nada hoy):
cámaras y scroll; gamepad; mapas de acciones; entidades y bloques; espacios; relojes y paso
fijo; eventos; **animación de sprites y tweens**; layout y foco de UI; manifiesto y escenas como
dato; grafo de dependencias de assets; física de plataformas; turnos, rangos y pathfinding;
inventario y estadísticas; partículas; reflexión de bloques; autoría en el editor; localización
del texto de interfaz.

### 5.5 Coste, honestamente

`src/` son ~13.400 líneas. Estimación gruesa de qué les pasa:

| | Líneas aprox. | |
|---|---|---|
| Se conserva (se mueve) | ~7.000 | `core`, `platform`, `text`, batching, backends, `resource`, baker |
| Se refactoriza | ~3.000 | API de render, input, editor, guardado, UI |
| Se reemplaza o borra | ~3.400 | `vm/`, `game/`, `main.cpp`, `script/` (reparto) |
| Nuevo | bastante más que lo anterior junto | todo el §5.4 "falta implementar" |

Es decir: **se conserva algo más de la mitad del código y se tira la parte que define el
producto.** Eso es coherente con lo que pediste (la prioridad 9 es la última) y conviene tenerlo
claro antes de aprobar: esto no es una refactorización, es un motor nuevo con los cimientos y
los servicios del actual, que resultan ser justo las dos partes más caras de hacer bien.

---

### 5.6 P0 — ejecutado

Hecho, con la suite verde en las tres configuraciones y sin cambios de comportamiento en el
juego salvo los tres que se detallan abajo (los tres eran defectos).

**Rename.** `SystemZ` (ADR-0075). 54 macros `VN_*` → `SZ_*`; objetivos `vne_base`/
`vne_script_tools`/`vne_game`/`vne_bake`/`vne_tests` → `sz_engine`/`sz_content`/`sz_runtime`/
`sz_bake`/`sz_tests`; `project(vne)` → `project(SystemZ)`; variables y funciones de CMake
`VNE_*`/`vne_*` → `SZ_*`/`sz_*`. El directorio del repositorio se queda como está.

**Los tres ciclos de dependencia, rotos.** El mapa de módulos es ahora un **orden total sin
ciclos**, medido por un test:

| Antes | Ahora | Cómo |
|---|---|---|
| `assets` ↔ `render`/`text`/`audio` | `vfs`(3) → … → `assets`(8) | `pak` baja a `vfs/`, que solo entrega bytes; el caché tipado se queda arriba en `assets/` |
| `vm` ↔ `script` | `lua`(10) → `vm`(9) | `lua_bindings` sale de la carpeta de herramientas a `lua/`, y `vm.cpp` deja de incluirlo: llama a `g_script_call_hook`, que `lua_init()` instala (mismo patrón que `g_editor_render_hook`) |
| `script` → `game` | `script` → `core`, `formats` | los layouts en disco (`cmd.h`, `state.h`, `map_format.h`) se mudan a `formats/`, que es lo único que herramienta offline y runtime comparten |

**Reestructuración.** `base/`→`core/`; `gfx/`→`rhi/` (backend de GPU, shaders) + `render/`
(lotes, atlas, texturas), con los identificadores renombrados de paso —`gfx_backend_*`→`rhi_*`,
`gfx_*`→`render_*`, `GfxLayer`→`RenderLayer`— porque P0 es el momento barato para un rename
mecánico; `formats/` y `vfs/` nuevos; `lua/` nuevo. Borrados los directorios vacíos `shaders/`
y `src/modes/`, que llevaban quince hitos existiendo para nada.

**Cómo se enforcea, y una desviación del plan escrito.** El plan decía "partir `sz_engine` en
objetivos de enlace por capa". **No se ha hecho, a propósito:** `vm/`, `game/` y `script/` se
desmantelan en P1–P5, así que congelar ahora un grafo de trece objetivos sería construirlo dos
veces. En su lugar, `tests/test_layering.cpp` declara el orden total de capas y **falla el
build si alguien lo rompe**, incluida la regla extra de que las herramientas offline solo
pueden depender de `core/` y `formats/`. Es más fuerte que los objetivos de enlace en dos
sentidos: ve las violaciones **en cabeceras**, que el enlazador no ve nunca, y detecta ciclos
por construcción. Se comprobó que no pasa en vacío añadiendo una violación deliberada
(`core/rng.h` incluyendo `render/render.h`), que reporta con archivo, módulo y rangos. Los
objetivos por capa se mueven a P2, cuando el grafo de módulos ya sea el definitivo.

**Tres defectos encontrados y arreglados.**

1. **El banco de pruebas de 5000 sprites de M1 seguía dibujándose en cada frame del juego**,
   cuatro hitos después de servir para algo. Borrado del bucle de frame, y el criterio de
   aceptación de M1 convertido en `tests/test_batching.cpp`, que es donde debía estar:
   **`draw_calls=1` con 5000 sprites encolados**, verificado en las tres configuraciones. Lo
   que se ve en el juego real: `sprites` pasa de **5158 a 123** por frame y `draw_calls` de 8 a
   6 reproduciendo la sesión de teclado, y de 5088 a **49** con la de ratón.
2. **`--autoplay-script` no llamaba a `symbols_load()`**, así que todo `@lua` de un guion
   reportaba "la variable no existe" y la ejecución **salía con 0 igualmente**. La herramienta
   con la que se verifican los guiones desde M3 daba por bueno un Lua que no funcionaba. Una
   línea.
3. El texto de demostración de M2 y su efecto de máquina de escribir seguían dibujándose
   encima del juego. Borrados junto al resto del contenido incrustado en `main()`.

**Números.** 96 archivos de runtime escaneados por el test de capas, 197 dependencias entre
módulos, **0 violaciones**; 14 archivos de herramientas, 12 dependencias, todas a `core/` o
`formats/`. 208/208 tests en Dev, 204/204 en Ship, 207/208 en Debug+ASan (solo el de
rendimiento de M2, no representativo sin optimizar, ADR-0018), **0 reportes de ASan**. Los
cuatro guiones de demo completan (185/25/12/11 comandos) y las dos sesiones grabadas se
reproducen con `heap_allocs_frame_max=0`.

**Lo que P0 no hizo:** `main.cpp` sigue siendo una función-dios de ~600 líneas con el cableado
de los cinco modos dentro. Se disuelve en P2, cuando existan el registro de sistemas y
`app/host.cpp`; vaciarla antes no tendría dónde poner las piezas.

---

## 6. Riesgos vivos, y qué hacemos con ellos

Las cuatro advertencias de la propuesta original, con su resolución.

1. **El alcance era enorme.** Resuelto por ADR-0076, y con una reducción **mayor** que la que
   propuse: la v1 se limita a novela visual y se exige que sea *usable*, no solo que funcione.
   RPG top-down queda como v2; táctico, plataformas y minijuegos como demostración. **Riesgo
   residual:** implementar un solo paradigma hasta la v1 retrasa el descubrimiento de fugas de
   núcleo. Mitigado con la sonda `PX` (§5.3), que no es negociable precisamente por eso.
2. **"Multipropósito" tiene un coste de ergonomía.** Un motor que no asume género no puede ser
   tan cómodo como RPG Maker para hacer un RPG. La ergonomía sale del **editor y de las
   features**, nunca del núcleo. Si el núcleo empieza a incorporar comodidades de un género, es
   la señal de que el diseño se está degradando (§3.6, regla 3). **Vivo y hay que vigilarlo en
   cada revisión.**
3. **La regla de cero heap.** Resuelto por ADR-0077: frame estacionario en cero, más fase de
   carga explícita y presupuestada, más capacidades declaradas y validadas al hornear.
4. **El editor se va a comer más tiempo que el runtime.** Es lo normal y casi siempre se
   subestima, y con la v1 definida como "usable" ahora está **dentro** del camino crítico
   (P9), no después. La mitigación es la de §4.5: inspector genérico derivado de la reflexión
   de bloques, y ningún panel escrito a mano si puede derivarse de un `BlockDesc`. **Es el
   riesgo de calendario más grande de la v1.**

Dos riesgos que aparecen al aprobar el plan y que conviene tener anotados:

5. **P1 es el único punto del plan sin vuelta atrás cómoda.** Cambiar el modelo de estado toca
   guardado, rollback, editor y todas las features a la vez. La red es el test de replay de M4
   (guardar y recargar en cada uno de los 185 comandos de `demo.vns` y comparar byte a byte) y
   los fixtures de `tests/saves/`. Si ese test pasa después de P1, la migración es correcta; si
   no se puede hacer pasar, hay que parar y replantear antes de seguir a P2.
6. **La pérdida de la exhaustividad del `switch`** (ADR-0078) quita una red que ya estaba
   agujereada (no funciona bajo MSVC) pero que sí atrapaba despistes al añadir comandos. La
   compensación tiene que ser real: el rechazo del baker ante un opcode no declarado es la
   mitad que importa, y hay que escribirla en la misma fase que el registro de rangos, no
   después.

---

## 7. Decisiones tomadas

Las cinco preguntas abiertas, resueltas. Detalle en ADR-0075 a ADR-0079.

| Pregunta | Resolución |
|---|---|
| ¿Modelo conceptual de §3? | **Aprobado.** Tres unidades de composición (feature / bloque+sistema / contexto) más dos portadores de restricción (space / clock). |
| ¿Nombre? | **SystemZ**, sin espacios. Macros `SZ_*`, objetivos `sz_*`, funciones con prefijo de módulo y sin prefijo de motor. El directorio del repositorio no se renombra. |
| ¿Las dos pérdidas de garantía? | **Aprobadas explícitamente.** Regla de heap precisada (ADR-0077) y tabla de opcodes validada al hornear y al cargar en lugar del `switch` exhaustivo (ADR-0078). |
| ¿Alcance de la v1? | **Usable para novela visual.** RPG top-down en la v2; el resto, demostración. Sube el proyecto-como-dato y el editor al camino crítico de la v1. |
| ¿De dónde sale el texto de interfaz? | **Un catálogo, dos formas de clave**: hash del original para el texto de contenido (que debe invalidarse al reescribirse) y nombre explícito para el de interfaz (que no debe). Cada feature declara su tabla de strings. |

**P0 ejecutado** (detalle y números en §5.6). **Fase activa: P1 — mundo y bloques.**
