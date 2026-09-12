#include "platform/input_record.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace {

constexpr u32 k_magic   = 0x43524E56u;  // 'VNRC'
// v2 (M15, etapa del raton): los bytes no cambian, pero el SIGNIFICADO de mouse_x/mouse_y
// si — pasan de pixeles de ventana a coordenadas virtuales 1920x1080 (ADR-0070). Un .vnrec
// v1 se reproduciria con el raton en el sitio equivocado, asi que se rechaza en vez de
// aceptarse a medias: es el mismo criterio que con el .vnc (se regenera, no se migra), y una
// grabacion de input es un artefacto de desarrollo, no dato de jugador.
constexpr u32 k_version = 2;

// Tope de registros, no de frames: cada registro puede cubrir miles de frames identicos. Un
// array fijo en vez de un vector porque grabar ocurre dentro del bucle de frame y la regla de
// cero heap (SPEC.md #4) vale tambien aqui.
constexpr u32 k_max_records = 16384;

struct Record {
    u32        repeat;
    InputState state;
};

// Grabacion en curso.
bool   g_recording = false;
char   g_record_path[256];
Record g_records[k_max_records];
u32    g_record_count = 0;

// Reproduccion en curso.
bool   g_replaying      = false;
Record g_replay[k_max_records];
u32    g_replay_count   = 0;
u32    g_replay_record  = 0;  // registro actual
u32    g_replay_repeat  = 0;  // cuantos frames de ese registro se han servido ya
u32    g_replay_frames  = 0;  // total de frames de la sesion

bool same_input(const InputState& a, const InputState& b) {
    // memcmp es valido porque InputState lleva relleno explicito (ver input.h): sin el, dos
    // estados logicamente iguales podrian diferir en los huecos de alineacion y ningun frame
    // se fundiria con el siguiente.
    return std::memcmp(&a, &b, sizeof(InputState)) == 0;
}

}  // namespace

bool input_record_begin(const char* path) {
    std::snprintf(g_record_path, sizeof(g_record_path), "%s", path);
    g_record_count = 0;
    g_recording    = true;
    log_info("input_record: grabando en '%s' (paso fijo de %.4f s)", path,
             static_cast<double>(k_input_record_dt));
    return true;
}

void input_record_frame(const InputState& state) {
    if (!g_recording) {
        return;
    }
    if (g_record_count > 0 && same_input(g_records[g_record_count - 1].state, state)) {
        g_records[g_record_count - 1].repeat += 1;
        return;
    }
    if (g_record_count >= k_max_records) {
        log_error("input_record: se alcanzo el tope de %u registros; la sesion se corta aqui",
                  k_max_records);
        input_record_end();
        return;
    }
    g_records[g_record_count].repeat = 1;
    g_records[g_record_count].state  = state;
    g_record_count += 1;
}

void input_record_end() {
    if (!g_recording) {
        return;
    }
    g_recording = false;

    std::FILE* f = std::fopen(g_record_path, "wb");
    if (f == nullptr) {
        log_error("input_record: no se pudo escribir '%s'", g_record_path);
        return;
    }
    f32 dt = k_input_record_dt;
    std::fwrite(&k_magic, sizeof(u32), 1, f);
    std::fwrite(&k_version, sizeof(u32), 1, f);
    std::fwrite(&dt, sizeof(f32), 1, f);
    std::fwrite(&g_record_count, sizeof(u32), 1, f);
    if (g_record_count > 0) {
        std::fwrite(g_records, sizeof(Record), g_record_count, f);
    }
    std::fclose(f);

    u32 frames = 0;
    for (u32 i = 0; i < g_record_count; ++i) {
        frames += g_records[i].repeat;
    }
    log_info("input_record: '%s' escrito (%u frames en %u registros)", g_record_path, frames,
             g_record_count);
}

bool input_record_active() {
    return g_recording;
}

bool input_replay_begin(const char* path) {
    g_replaying     = false;
    g_replay_count  = 0;
    g_replay_record = 0;
    g_replay_repeat = 0;
    g_replay_frames = 0;

    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        log_error("input_replay: no se encontro '%s'", path);
        return false;
    }
    u32 magic = 0, version = 0, count = 0;
    f32 dt = 0.0f;
    bool ok = std::fread(&magic, sizeof(u32), 1, f) == 1;
    ok = ok && std::fread(&version, sizeof(u32), 1, f) == 1;
    ok = ok && std::fread(&dt, sizeof(f32), 1, f) == 1;
    ok = ok && std::fread(&count, sizeof(u32), 1, f) == 1;
    if (!ok || magic != k_magic) {
        std::fclose(f);
        log_error("input_replay: '%s' no es una sesion valida (magic)", path);
        return false;
    }
    if (version != k_version) {
        // Se rechaza, no se migra: es un artefacto de desarrollo, como un .vnc.
        std::fclose(f);
        log_error("input_replay: '%s' es version %u, se esperaba %u", path, version, k_version);
        return false;
    }
    if (count > k_max_records) {
        std::fclose(f);
        log_error("input_replay: '%s' dice tener %u registros, mas del tope de %u", path, count,
                  k_max_records);
        return false;
    }
    if (count > 0 && std::fread(g_replay, sizeof(Record), count, f) != count) {
        std::fclose(f);
        log_error("input_replay: '%s' esta truncado", path);
        return false;
    }
    std::fclose(f);

    g_replay_count = count;
    for (u32 i = 0; i < count; ++i) {
        g_replay_frames += g_replay[i].repeat;
    }
    g_replaying = count > 0;
    log_info("input_replay: '%s' cargado (%u frames en %u registros)", path, g_replay_frames,
             count);
    return g_replaying;
}

bool input_replay_next(InputState* out_state) {
    if (!g_replaying || g_replay_record >= g_replay_count) {
        g_replaying = false;
        return false;
    }
    *out_state = g_replay[g_replay_record].state;
    g_replay_repeat += 1;
    if (g_replay_repeat >= g_replay[g_replay_record].repeat) {
        g_replay_record += 1;
        g_replay_repeat = 0;
    }
    return true;
}

bool input_replay_active() {
    return g_replaying;
}

u32 input_replay_frame_count() {
    return g_replay_frames;
}
