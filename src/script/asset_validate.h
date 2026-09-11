#pragma once
#include <string>
#include <vector>

#include "script/parser.h"

// Validacion de nombres de asset en tiempo de compilacion (M13). **Cierra ADR-0022**, que
// desde M3 dejaba pasar cualquier nombre mal escrito: solo las etiquetas se validaban, y
// `@show mrata neutral` compilaba sin una queja para luego no dibujar nada en pantalla —
// un fallo silencioso en runtime, justo lo que el skill vne-script-dsl prohibe.
//
// Vive aparte del parser a proposito: `parse_script` no tiene por que saber que existe un
// atlas horneado, y separarlo permite testear la validacion con un registro fabricado a
// mano en vez de depender del atlas real.
//
// Solo herramientas offline (`vne_script_tools`): el juego nunca valida nada de esto, lee
// IDs ya internados de un `.vnc` (skill vne-script-dsl, "cero parsing en release").

// Convencion de nombres (ADR de M13): un actor con su pose es el sprite
// `actor_<actor>_<pose>`, y un fondo es `bg_<nombre>`. El prefijo hace falta porque actores
// y fondos comparten el mismo espacio de nombres plano del atlas, que es el del archivo
// PNG; sin el, un fondo llamado "marta" chocaria con un actor llamado "marta".
std::string asset_sprite_name_for_actor(const std::string& actor, const std::string& pose);
std::string asset_sprite_name_for_background(const std::string& bg);

// Nombres logicos disponibles. Se construye leyendo la tabla de nombres de
// `atlas_00.bin` v3 (la que M13 le añadio; ver tools/bake/main.cpp), que es el registro de
// assets: no hay un segundo archivo que pueda desincronizarse.
struct AssetRegistry {
    std::vector<std::string> sprite_names;

    bool contains(const std::string& name) const;

    // Devuelve false si el archivo falta o no es un atlas v3. El llamante decide si eso es
    // fatal: `vne_bake script` avisa y sigue sin validar, para no volver imposible compilar
    // un guion en un arbol donde todavia no se ha horneado el atlas.
    static bool load_from_atlas_bin(const char* path, AssetRegistry* out);
};

// Añade a `out_errors` un error por cada `@show`/`@bg` cuyo sprite no este en el registro,
// con archivo y linea, exactamente igual que ya hace una etiqueta desconocida.
void validate_asset_names(const std::vector<ParsedInstr>& instructions,
                           const std::string& file_name, const AssetRegistry& registry,
                           std::vector<ParseError>* out_errors);
