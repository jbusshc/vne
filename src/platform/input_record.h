#pragma once
#include "platform/input.h"

// Grabacion y reproduccion de input (M15, SPEC.md #12). **Cierra la limitacion arrastrada
// desde M4**: F5/F9, el rollback, SaveLoadMode, BacklogMode, MenuMode, el movimiento con WASD
// y el cambio de idioma se habian verificado siempre por su logica interna, nunca pulsando
// teclas, porque este entorno no permite inyectar input en una ventana real. Con una sesion
// grabada, la secuencia de pulsaciones se reproduce dentro de la suite de tests.
//
// **Paso fijo, a proposito.** Una grabacion se hace y se reproduce a 1/60 exacto. Una sesion
// grabada existe para ser REPRODUCIBLE, y con un dt variable la reproduccion divergiria del
// original en cuanto un temporizador (una transicion, el modo auto, un fundido) cayera en otro
// frame. El precio es que un .vnrec no sirve como traza de rendimiento; no pretende serlo.
//
// Formato `.vnrec`:
//   magic 'VNRC' (4) | version (4) | fixed_dt (f32) | record_count (4)
//   record_count veces: { u32 repeat; InputState state; }
//
// Los frames consecutivos con el MISMO InputState se funden en un solo registro con su
// cuenta. En una sesion normal casi ningun frame tiene input, asi que una sesion de treinta
// segundos ocupa unos pocos kilobytes en vez de dos megas.
//
// Lo que se graba es el InputState que VE LA LOGICA DE JUEGO, no el que entrega la
// plataforma: mouse_x/mouse_y vienen en coordenadas virtuales 1920x1080, ya sin letterbox
// (main.cpp los convierte con render_window_to_virtual antes de grabar). Asi una sesion se
// reproduce igual en una ventana de otro tamano, y se puede reproducir sin ventana ninguna
// dentro de la suite de tests. Por eso el formato subio a v2 en M15 sin cambiar un solo byte
// de layout: cambio el significado, que para un formato en disco cuenta igual.

constexpr f32 k_input_record_dt = 1.0f / 60.0f;

// --- Grabar -----------------------------------------------------------------------------

// Empieza a grabar en `path`. Devuelve false si no se puede escribir; el juego sigue sin
// grabar, nunca es fatal.
bool input_record_begin(const char* path);

// Registra el input de este frame. No hace nada si no se esta grabando.
void input_record_frame(const InputState& state);

// Cierra y escribe el archivo. Idempotente.
void input_record_end();

bool input_record_active();

// --- Reproducir -------------------------------------------------------------------------

// Carga una sesion. Devuelve false si falta o esta corrupta.
bool input_replay_begin(const char* path);

// Escribe en *out_state el input del siguiente frame grabado. Devuelve false cuando la
// sesion se acaba, momento en el que el llamante decide si sale o sigue en vivo.
bool input_replay_next(InputState* out_state);

bool input_replay_active();

// Cuantos frames tiene la sesion cargada en total. Para diagnostico y tests.
u32 input_replay_frame_count();
