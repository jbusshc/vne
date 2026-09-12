#include <doctest/doctest.h>

#include <cstring>
#include <ostream>
#include <set>
#include <vector>
#include <string>

#include "core/arena.h"
#include "platform/files.h"
#include "test_config.h"

// P0 del rediseño: **la regla de capas la comprueba una máquina, no la disciplina.**
//
// `SPEC.md` §5 declaraba cinco capas estrictas desde M0 y la regla "las capas superiores
// conocen a las inferiores, nunca al revés". Medido al empezar el rediseño, la realidad tenía
// **tres ciclos**: `assets` <-> `render`/`text`/`audio` (porque `pak` estaba en la carpeta
// equivocada), `vm` <-> `script` (porque `lua_bindings`, que es runtime, vivía en la carpeta de
// herramientas offline) y `script` -> `game` (la herramienta de horneado dependía del código
// del juego). Se rompieron los tres en P0; esto es lo que evita que vuelvan.
//
// Por qué un test y no objetivos de enlace separados: un test detecta también las violaciones
// **en cabeceras**, que el enlazador no ve nunca, y detecta ciclos por construcción. Los
// objetivos por capa llegan en P2, cuando `vm/`, `game/` y `script/` ya se hayan desmantelado y
// el grafo de módulos sea el definitivo — congelarlo ahora sería construirlo dos veces.

namespace {

// Orden total de capas del runtime. Un módulo solo puede incluir de un rango **estrictamente
// menor**, o de sí mismo. Un orden total en vez de una lista de aristas permitidas es más
// restrictivo a propósito: descarta los ciclos por construcción, y obliga a justificar una
// dependencia nueva moviendo un módulo de sitio en vez de añadiendo una excepción.
//
// A medida que avance el rediseño, esta tabla es el sitio donde se declara la arquitectura:
// `world`, `state`, `input`, `ui`, `app` y `features/*` se insertan aquí (REDESIGN.md §4.1).
struct Layer {
    const char* name;
    int         rank;
};

constexpr Layer k_layers[] = {
    {"core", 0},      // arenas, handles, pools, matemática, presupuestos
    {"formats", 1},   // layouts en disco, compartidos por runtime y herramientas
    {"platform", 2},  // ventana, input, reloj, filesystem, hilos
    {"vfs", 3},       // resolver un nombre lógico a bytes (.pak o directorio suelto)
    {"rhi", 4},       // backend de GPU tras sokol_gfx
    {"render", 5},    // lotes, atlas, texturas
    {"text", 6},      // FreeType + HarfBuzz, caché de glifos, layout
    {"audio", 7},     // mezclador y buses
    {"assets", 8},    // caché tipada e integración de las cargas del hilo de IO
    {"vm", 9},        // intérprete de comandos (se disuelve en world/ + features en P5)
    {"lua", 10},      // scripting (pasa a feature opcional en P5)
    {"game", 11},     // modos (pasan a contextos + features en P2/P5)
    {"editor", 12},   // herramienta de desarrollo, excluida del build en Ship
};

// `src/script/` no está en la tabla porque no es una capa del runtime: es la librería de
// herramientas offline (`sz_content`), que se enlaza **sin SDL3 a propósito**. Su regla es
// distinta y más fuerte, y vive en su propio test más abajo.
constexpr const char* k_tools_module          = "script";
constexpr const char* k_tools_allowed[]       = {"core", "formats"};

int rank_of(const char* module) {
    for (const Layer& l : k_layers) {
        if (std::strcmp(l.name, module) == 0) {
            return l.rank;
        }
    }
    return -1;
}

// Lo que un directorio de módulo incluye de otros módulos del proyecto: pares
// (archivo, módulo incluido). Es toda la información que los dos tests necesitan.
struct Edge {
    std::string file;
    std::string module;
};

struct Scan {
    Arena*            arena  = nullptr;
    std::string       module;
    std::vector<Edge> edges;
    u32               files  = 0;
    std::string       errors;
};

// Extrae el módulo de un `#include "modulo/archivo.h"`. Devuelve "" si la línea no es un
// include de este proyecto (cabecera de sistema, de terceros, o sin barra).
std::string included_module(const char* line, usize len) {
    const char* needle = "#include \"";
    usize       nlen   = std::strlen(needle);
    if (len < nlen || std::strncmp(line, needle, nlen) != 0) {
        return "";
    }
    std::string mod;
    usize       i = nlen;
    while (i < len && line[i] != '/' && line[i] != '"') {
        mod.push_back(line[i]);
        i += 1;
    }
    return (i < len && line[i] == '/') ? mod : std::string();
}

bool ends_with(const char* s, const char* suffix) {
    usize ls = std::strlen(s), lf = std::strlen(suffix);
    return ls >= lf && std::strcmp(s + ls - lf, suffix) == 0;
}

void scan_file(void* userdata, const char* /*name_no_ext*/, const char* full_name) {
    Scan* scan = static_cast<Scan*>(userdata);
    if (!ends_with(full_name, ".h") && !ends_with(full_name, ".cpp")) {
        return;
    }

    std::string path = std::string(SZ_SOURCE_DIR) + "/src/" + scan->module + "/" + full_name;
    u8*         data = nullptr;
    usize       size = 0;
    if (!file_read_all(path.c_str(), scan->arena, &data, &size)) {
        scan->errors += "no se pudo leer " + path + "\n";
        return;
    }
    scan->files += 1;

    usize line_start = 0;
    for (usize i = 0; i <= size; ++i) {
        if (i != size && data[i] != '\n') {
            continue;
        }
        if (i > line_start) {
            std::string dep =
                included_module(reinterpret_cast<const char*>(data) + line_start, i - line_start);
            if (!dep.empty() && dep != scan->module) {
                scan->edges.push_back(Edge{full_name, dep});
            }
        }
        line_start = i + 1;
    }
}

Scan scan_module(const char* module, Arena* arena) {
    Scan scan;
    scan.arena  = arena;
    scan.module = module;
    std::string dir = std::string(SZ_SOURCE_DIR) + "/src/" + module;
    dir_list_by_extension(dir.c_str(), nullptr, scan_file, &scan);
    return scan;
}

}  // namespace

TEST_CASE("capas: ningun modulo del runtime depende de otro de su mismo nivel o superior") {
    Arena arena = arena_create(8 * 1024 * 1024, "layering");

    std::string violations;
    u32         total_files = 0;
    u32         total_edges = 0;

    for (const Layer& layer : k_layers) {
        Scan scan = scan_module(layer.name, &arena);

        // Un módulo declarado sin archivos significa que se renombró o se borró sin actualizar
        // k_layers, y entonces este test dejaría de vigilarlo **en silencio**.
        INFO("modulo: " << std::string(layer.name));
        CHECK_MESSAGE(scan.files > 0, "modulo declarado en k_layers sin ningun .h/.cpp");
        violations += scan.errors;

        for (const Edge& e : scan.edges) {
            int to = rank_of(e.module.c_str());
            if (to < 0) {
                violations += std::string(layer.name) + "/" + e.file + " incluye '" + e.module +
                              "/', que no es una capa declarada (si es nueva, ponla en "
                              "k_layers; si es una herramienta offline, no puede incluirse "
                              "desde el runtime)\n";
            } else if (to >= layer.rank) {
                violations += std::string(layer.name) + "/" + e.file + " incluye '" + e.module +
                              "/' (rango " + std::to_string(to) + " >= " +
                              std::to_string(layer.rank) + ")\n";
            }
        }
        total_files += scan.files;
        total_edges += static_cast<u32>(scan.edges.size());
    }

    MESSAGE("archivos escaneados: " << total_files
                                    << ", dependencias entre modulos: " << total_edges);
    INFO("violaciones:\n" << violations);
    CHECK(violations.empty());

    // Si esto no se cumple, el test no está mirando el árbol de verdad y su "cero violaciones"
    // no significa nada. Es la lección de M12: medir con el instrumento equivocado da 0
    // siempre, asigne o no.
    CHECK(total_files > 50);
    CHECK(total_edges > 50);

    arena_destroy(&arena);
}

TEST_CASE("capas: las herramientas offline no dependen de ningun servicio del runtime") {
    // `src/script/` se enlaza en `sz_content` **sin SDL3 a propósito**: es una herramienta
    // offline y no debe arrastrar ventana, GPU ni audio. Antes del rediseño incluía
    // `game/map_format.h` y `vm/state.h`, es decir dependía del código del juego. Ahora los
    // layouts en disco viven en `formats/`, que es lo único que herramienta y runtime
    // comparten.
    Arena arena = arena_create(2 * 1024 * 1024, "layering_tools");
    Scan  scan  = scan_module(k_tools_module, &arena);

    CHECK(scan.files > 0);
    CHECK(scan.errors.empty());

    std::set<std::string> allowed(std::begin(k_tools_allowed), std::end(k_tools_allowed));
    std::string           violations;
    for (const Edge& e : scan.edges) {
        if (allowed.find(e.module) == allowed.end()) {
            violations += std::string(k_tools_module) + "/" + e.file + " incluye '" + e.module +
                          "/': una herramienta offline solo puede depender de core/ y "
                          "formats/\n";
        }
    }
    INFO("violaciones:\n" << violations);
    CHECK(violations.empty());
    MESSAGE("archivos de herramientas escaneados: " << scan.files
                                                    << ", dependencias: " << scan.edges.size());

    arena_destroy(&arena);
}
