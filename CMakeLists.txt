cmake_minimum_required(VERSION 3.25)
project(vne LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Configuraciones propias del proyecto (SPEC.md #4). No son los tipos estandar de CMake,
# asi que definimos sus flags de optimizacion a mano.
if(MSVC)
  set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi")
  set(CMAKE_CXX_FLAGS_DEV "/O2 /Zi")
  set(CMAKE_CXX_FLAGS_SHIP "/O2")
else()
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
  set(CMAKE_CXX_FLAGS_DEV "-O2 -g")
  set(CMAKE_CXX_FLAGS_SHIP "-O3")
endif()

# Algunas dependencias de terceros (doctest v2.4.11) todavia declaran
# cmake_minimum_required por debajo de lo que CMake moderno acepta sin este puente.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(cmake/CPM.cmake)

CPMAddPackage(
  NAME SDL3
  GITHUB_REPOSITORY libsdl-org/SDL
  GIT_TAG release-3.2.30
  OPTIONS
    "SDL_SHARED OFF"
    "SDL_STATIC ON"
    "SDL_TEST_LIBRARY OFF"
    "SDL_EXAMPLES OFF"
)
if(TARGET SDL3::SDL3)
  set(VNE_SDL3_TARGET SDL3::SDL3)
else()
  set(VNE_SDL3_TARGET SDL3::SDL3-static)
endif()

CPMAddPackage(
  NAME HandmadeMath
  GITHUB_REPOSITORY HandmadeMath/HandmadeMath
  GIT_TAG v2.0.0
  DOWNLOAD_ONLY YES
)
if(HandmadeMath_ADDED)
  add_library(HandmadeMath INTERFACE)
  target_include_directories(HandmadeMath INTERFACE ${HandmadeMath_SOURCE_DIR})
endif()

CPMAddPackage(
  NAME doctest
  GITHUB_REPOSITORY doctest/doctest
  GIT_TAG v2.4.12
)

# sokol_gfx.h pineado a un commit anterior al refactor de "sg_view" (23-ago-2025) que
# reescribio la API de bindings: este commit usa la API clasica images[]/samplers[]/
# image_sampler_pairs[] sobre la que estan escritos src/gfx/shaders.h y texture.cpp.
CPMAddPackage(
  NAME sokol
  GITHUB_REPOSITORY floooh/sokol
  GIT_TAG 05a7340ee7e1c686225c6c0421b4cedcb1a142ff
  DOWNLOAD_ONLY YES
)
if(sokol_ADDED)
  add_library(sokol_gfx INTERFACE)
  target_include_directories(sokol_gfx INTERFACE ${sokol_SOURCE_DIR})
endif()

# qoi.h (dominio publico): formato de textura horneada elegido en ADR-0008.
CPMAddPackage(
  NAME qoi
  GITHUB_REPOSITORY phoboslab/qoi
  GIT_TAG 97bacc86a9c4abf5a2d452102dc26546c4c670b9
  DOWNLOAD_ONLY YES
)
if(qoi_ADDED)
  add_library(qoi INTERFACE)
  target_include_directories(qoi INTERFACE ${qoi_SOURCE_DIR})
endif()

# stb_image.h: SPEC.md #3 dice "solo en herramientas offline, nunca en runtime". Ahora
# hace falta de verdad: assets_src/png/ tiene sprites reales (Kenney UI Pack, CC0) y
# vne_bake los empaqueta con un shelf packer real en vez de la rejilla procedural de M1
# (ADR-0011 / ADR-0025, ver docs/DECISIONS.md).
CPMAddPackage(
  NAME stb
  GITHUB_REPOSITORY nothings/stb
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  DOWNLOAD_ONLY YES
)
if(stb_ADDED)
  add_library(stb_image INTERFACE)
  target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})
endif()

# miniaudio.h (dominio publico / MIT-0): decodifica OGG/WAV/FLAC y mezcla, SPEC.md #3/#7.3.
# Cabecera unica, igual que qoi.h/stb_image.h: STB_IMPLEMENTATION-style, un solo .cpp
# (src/audio/audio.cpp) define MINIAUDIO_IMPLEMENTATION.
CPMAddPackage(
  NAME miniaudio
  GITHUB_REPOSITORY mackron/miniaudio
  GIT_TAG 0.11.21
  DOWNLOAD_ONLY YES
)
if(miniaudio_ADDED)
  add_library(miniaudio INTERFACE)
  target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})
endif()

# Dear ImGui, rama docking (SPEC.md #3: "toda la UI de desarrollo"), solo para Debug/Dev.
# SPEC.md #6 dice literalmente "en Ship, todo el codigo de src/editor/ queda excluido del
# build": en vez de compilarlo vacio tras un #if defined(VN_EDITOR) (que dejaria simbolos
# de ImGui como codigo muerto pero presente), directamente no se añade ni ImGui ni
# src/editor/ al target en una configuracion Ship — la unica forma de garantizar "cero
# simbolos de ImGui" de verdad, no solo "cero uso". CMAKE_BUILD_TYPE es fijo por
# configuracion (un directorio de build por preset, ver CMakePresets.json), asi que esta
# comprobacion en tiempo de configuracion es fiable aqui.
set(VNE_EDITOR_ENABLED $<NOT:$<CONFIG:Ship>>)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Ship")
  CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG v1.91.5-docking
    DOWNLOAD_ONLY YES
  )
  if(imgui_ADDED)
    add_library(imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/imgui_demo.cpp
    )
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
    # ImGui no compila limpio bajo -Wall -Wextra -Werror ajenos (SPEC.md #4 solo exige
    # eso en el codigo propio del proyecto, ver el comentario de VNE_WARNING_FLAGS mas
    # abajo): se compila con los flags por defecto del compilador, sin vne_apply_project_flags.
  endif()
endif()

# FreeType antes que HarfBuzz: el CMakeLists.txt de HarfBuzz detecta el target "freetype"
# ya existente y activa HB_HAVE_FREETYPE automaticamente (ver docs/DECISIONS.md M2).
CPMAddPackage(
  NAME freetype
  GITHUB_REPOSITORY freetype/freetype
  GIT_TAG VER-2-13-3
  OPTIONS
    "FT_DISABLE_ZLIB ON"
    "FT_DISABLE_BZIP2 ON"
    "FT_DISABLE_PNG ON"
    "FT_DISABLE_HARFBUZZ ON"
    "FT_DISABLE_BROTLI ON"
)

CPMAddPackage(
  NAME harfbuzz
  GITHUB_REPOSITORY harfbuzz/harfbuzz
  GIT_TAG 9.0.0
  OPTIONS
    "HB_BUILD_UTILS OFF"
    "HB_HAVE_GLIB OFF"
    "HB_HAVE_ICU OFF"
    "HB_HAVE_CAIRO OFF"
    "HB_HAVE_GRAPHITE2 OFF"
)

# Lua 5.4 + sol2 (SPEC.md #3, #9.4): el repositorio oficial de Lua no trae CMakeLists.txt,
# asi que se usa el fork de walterschell (empaquetado CMake/CPM-friendly del mismo codigo
# fuente sin modificar) que ya usan otros proyectos que integran Lua via CPM.
CPMAddPackage(
  NAME lua
  GITHUB_REPOSITORY walterschell/Lua
  GIT_TAG v5.4.6
  OPTIONS
    "LUA_ENABLE_SHARED OFF"
)

# sol2 es solo cabeceras: envuelve la API C de Lua ya traida arriba.
CPMAddPackage(
  NAME sol2
  GITHUB_REPOSITORY ThePhD/sol2
  GIT_TAG v3.3.0
  DOWNLOAD_ONLY YES
)
if(sol2_ADDED)
  add_library(sol2 INTERFACE)
  target_include_directories(sol2 INTERFACE ${sol2_SOURCE_DIR}/include)
endif()

# En MSVC, /fsanitize=address requiere el runtime clang_rt.asan de la instalacion. No todas
# las instalaciones de Visual Studio lo traen (falta si no se marco el componente individual
# "C++ AddressSanitizer" en el instalador). Lo detectamos para no romper el build entero por
# un componente opcional ausente; ver docs/DECISIONS.md.
set(VNE_ASAN_AVAILABLE ON)
if(MSVC)
  find_file(VNE_MSVC_ASAN_LIB
    NAMES clang_rt.asan_dynamic_runtime_thunk-x86_64.lib clang_rt.asan_dynamic_runtime_thunk.lib
    PATHS "$ENV{VCToolsInstallDir}lib/x64" "$ENV{VCToolsInstallDir}lib/onecore/x64"
  )
  if(NOT VNE_MSVC_ASAN_LIB)
    set(VNE_ASAN_AVAILABLE OFF)
    message(WARNING
      "AddressSanitizer no disponible: falta el runtime clang_rt.asan en esta instalacion "
      "de MSVC (componente individual 'C++ AddressSanitizer' del VS Installer). La build "
      "Debug se compila SIN /fsanitize=address hasta que se instale.")
  endif()
endif()

# Flags del proyecto (SPEC.md #4). Se aplican solo a nuestros propios targets, nunca a
# dependencias de terceros traidas por CPM: SDL3 no compila limpio bajo -Werror ajeno.
if(MSVC)
  set(VNE_WARNING_FLAGS /W4 /WX)
  set(VNE_LANG_FLAGS /GR- /EHs-c-)
else()
  set(VNE_WARNING_FLAGS -Wall -Wextra -Werror)
  set(VNE_LANG_FLAGS -fno-exceptions -fno-rtti)
endif()

function(vne_apply_project_flags target)
  target_compile_options(${target} PRIVATE ${VNE_WARNING_FLAGS} ${VNE_LANG_FLAGS})
  target_compile_definitions(${target} PRIVATE
    $<$<CONFIG:Debug>:VN_DEBUG=1>
    $<$<CONFIG:Dev>:VN_DEBUG=1>
    $<$<CONFIG:Dev>:VN_EDITOR=1>
    $<$<CONFIG:Ship>:NDEBUG>
    $<$<CONFIG:Ship>:VN_SHIPPING=1>
  )
  if(MSVC)
    # fopen/fread/etc son estandar y portables; el aviso de "usa fopen_s" de MSVC no
    # aplica al resto de plataformas del proyecto.
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  if(VNE_ASAN_AVAILABLE)
    if(MSVC)
      target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/fsanitize=address>)
    else()
      target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
      target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
    endif()
  endif()
endfunction()

# Backend grafico por plataforma (SPEC.md #2). Sin sokol_app en el stack cerrado, el
# contexto de cada backend se crea a mano sobre la ventana SDL3 (ver ADR-0009: Metal/macOS
# queda fuera de M1 por no haber Mac disponible para escribirlo y probarlo).
if(WIN32)
  set(VNE_GFX_BACKEND_SOURCES src/gfx/gfx_backend_d3d11.cpp)
  set(VNE_GFX_BACKEND_DEFINE VNE_GFX_BACKEND_D3D11=1)
  set(VNE_GFX_SOKOL_BACKEND_DEFINE SOKOL_D3D11)
  set(VNE_GFX_BACKEND_LIBS d3d11 dxgi)
elseif(APPLE)
  set(VNE_GFX_BACKEND_SOURCES "")
  set(VNE_GFX_BACKEND_DEFINE VNE_GFX_BACKEND_NONE=1)
  set(VNE_GFX_SOKOL_BACKEND_DEFINE SOKOL_DUMMY_BACKEND)
  set(VNE_GFX_BACKEND_LIBS "")
  message(WARNING
    "Backend Metal no implementado todavia (ADR-0009): M1 no renderiza nada real en macOS.")
else()
  set(VNE_GFX_BACKEND_SOURCES src/gfx/gfx_backend_gl.cpp)
  set(VNE_GFX_BACKEND_DEFINE VNE_GFX_BACKEND_GL=1)
  set(VNE_GFX_SOKOL_BACKEND_DEFINE SOKOL_GLCORE)
  set(VNE_GFX_BACKEND_LIBS GL)
endif()

add_library(vne_base STATIC
  src/base/log.cpp
  src/base/arena.cpp
  src/base/heap_guard.cpp
  src/base/heap_guard_hooks.cpp
  src/base/math.cpp
  src/base/radix_sort.cpp
  src/base/crc32.cpp
  src/base/rng.cpp
  src/platform/window.cpp
  src/platform/input.cpp
  src/platform/files.cpp
  src/assets/pak.cpp
  src/assets/assets.cpp
  src/assets/hot_reload.cpp
  src/gfx/atlas.cpp
  src/gfx/gfx.cpp
  src/gfx/texture.cpp
  src/text/font.cpp
  src/text/glyph_cache.cpp
  src/text/layout.cpp
  src/text/catalog.cpp
  src/vm/vm.cpp
  src/vm/script_load.cpp
  src/vm/backlog.cpp
  src/vm/rollback.cpp
  src/vm/save.cpp
  src/script/lua_bindings.cpp
  src/audio/audio.cpp
  src/game/vn_mode.cpp
  src/game/backlog_mode.cpp
  src/game/menu_mode.cpp
  src/game/save_load_mode.cpp
  src/game/map_catalog.cpp
  src/game/map_mode.cpp
  ${VNE_GFX_BACKEND_SOURCES}
)
target_include_directories(vne_base PUBLIC src)
if(WIN32)
  # El backend WASAPI de miniaudio en Windows usa COM.
  set(VNE_AUDIO_BACKEND_LIBS ole32)
else()
  set(VNE_AUDIO_BACKEND_LIBS "")
endif()
target_link_libraries(vne_base
  PUBLIC ${VNE_SDL3_TARGET} HandmadeMath
  PRIVATE sokol_gfx qoi freetype harfbuzz ${VNE_GFX_BACKEND_LIBS} lua_static sol2 miniaudio ${VNE_AUDIO_BACKEND_LIBS}
)
target_compile_definitions(vne_base PRIVATE ${VNE_GFX_BACKEND_DEFINE})
# El backend de sokol_gfx solo debe estar activo en la unidad de traduccion que hace
# SOKOL_IMPL (gfx.cpp); definirlo a nivel de target es simple y no da falsos positivos
# porque ninguna otra .cpp de vne_base incluye sokol_gfx.h con SOKOL_IMPL.
set_source_files_properties(src/gfx/gfx.cpp PROPERTIES COMPILE_DEFINITIONS ${VNE_GFX_SOKOL_BACKEND_DEFINE})

# Editor (M8): src/editor/editor.cpp NO se añade al target en una configuracion Ship
# (SPEC.md #6, ver el bloque de ImGui mas arriba). En Debug/Dev si se añade y se compila
# (aunque su cuerpo entero este vacio bajo #if defined(VN_EDITOR) salvo en Dev, igual que
# gfx_backend_gl.cpp en Windows) para que los errores de tipos se detecten pronto, no solo
# en una build Dev.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Ship")
  target_sources(vne_base PRIVATE src/editor/editor.cpp)
  target_link_libraries(vne_base PRIVATE imgui)
  # sokol_imgui.h necesita saber el backend activo para elegir sus shaders embebidos,
  # igual que gfx.cpp con SOKOL_IMPL (mismo motivo, ver comentario de arriba).
  # VNE_SOURCE_DIR: el watcher de recarga de scripts necesita la ruta absoluta al .vns
  # fuente (assets_src/scripts/ nunca se copia al directorio de build, a diferencia de
  # png/ogg/ttf: vne_bake ya lo lee directo de CMAKE_SOURCE_DIR), o dependeria de que
  # vne_game se ejecute con el CWD exacto del directorio de build (fragil).
  set_source_files_properties(src/editor/editor.cpp PROPERTIES COMPILE_DEFINITIONS
    "${VNE_GFX_SOKOL_BACKEND_DEFINE};VNE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
  if(MSVC)
    # ImGui/sokol_imgui.h son codigo de terceros que llega a esta unidad de traduccion:
    # advertencias de conversion y variables locales sin usar en sus propias macros, no
    # un problema de nuestro codigo (mismo patron que sol2/doctest en otros CMakeLists).
    set_source_files_properties(src/editor/editor.cpp PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4267;/wd4996")
  endif()
endif()

vne_apply_project_flags(vne_base)

if(MSVC)
  # sol2 es codigo de terceros que llega via #include a esta unica unidad de traduccion
  # (SPEC.md #9.4): C5321 es su uso de literales UTF-8 con bytes altos en nombres
  # internos, y C4530 es el mismo problema de excepciones-en-ostream ya visto con
  # doctest en tests/CMakeLists.txt (basic_ostream::operator<< usa try/catch interno
  # aunque nunca lance, y el proyecto compila con /EHs-c-). Ninguno de los dos es un
  # problema de nuestro codigo.
  set_source_files_properties(src/script/lua_bindings.cpp PROPERTIES COMPILE_OPTIONS "/wd5321;/wd4530")
endif()

add_executable(vne_game src/main.cpp)
target_link_libraries(vne_game PRIVATE vne_base)
vne_apply_project_flags(vne_game)

# Codigo de herramientas offline: lexer/parser/compilador del DSL (SPEC.md #9.3) y el
# escaner de TMX de los mapas (ADR-0044). Solo se linkan en vne_bake y en los tests;
# vne_game nunca los ve — "cero parsing en release". std::string/vector son aceptables
# aqui porque este codigo jamas corre en el bucle de frame del juego. El escaner de mapas
# vive aqui, y no dentro de vne_bake, precisamente para que los tests puedan ejercitarlo.
add_library(vne_script_tools STATIC
  src/script/lexer.cpp
  src/script/parser.cpp
  src/script/compiler.cpp
  src/script/asset_validate.cpp
  src/script/hash_collisions.cpp
  src/script/map_bake.cpp
)
target_include_directories(vne_script_tools PUBLIC src)
vne_apply_project_flags(vne_script_tools)

# Herramienta offline: no enlaza vne_base (SDL3/sokol/gfx) porque no abre ventana ni
# dibuja nada, solo genera y codifica bytes. Solo toma prestado base/log.cpp de vne_base.
add_executable(vne_bake tools/bake/main.cpp src/base/log.cpp)
target_include_directories(vne_bake PRIVATE src)
target_link_libraries(vne_bake PRIVATE qoi vne_script_tools stb_image harfbuzz harfbuzz-subset)
vne_apply_project_flags(vne_bake)

# vne_bake busca assets_src/png/*.png con una ruta relativa a su propio CWD (ADR-0025):
# hace falta copiarlo al directorio de build igual que las fuentes, o siempre caeria al
# respaldo procedural aunque los PNGs reales existan en el arbol fuente.
file(GLOB VNE_PNG_ASSETS ${CMAKE_SOURCE_DIR}/assets_src/png/*.png)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/.vne_png_assets_copied
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_src/png
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets_src/png
          ${CMAKE_CURRENT_BINARY_DIR}/assets_src/png
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/.vne_png_assets_copied
  DEPENDS ${VNE_PNG_ASSETS}
  COMMENT "Copiando assets_src/png/ (Kenney UI Pack, CC0) al directorio de build"
)
add_custom_target(vne_copy_png_assets
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/.vne_png_assets_copied
)

# Un checkout limpio mas un build debe producir un juego ejecutable sin pasos manuales
# (skill vne-build-verify): vne_game depende del atlas de prueba horneado por vne_bake en
# el propio directorio de build, no de que alguien se acuerde de ejecutarlo a mano.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.qoi
         ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.bin
  COMMAND vne_bake
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS vne_bake ${CMAKE_CURRENT_BINARY_DIR}/.vne_png_assets_copied
  COMMENT "vne_bake: generando atlas (real si hay PNGs, procedural si no — ADR-0011/0025)"
)
add_custom_target(vne_bake_assets
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.qoi
)
add_dependencies(vne_bake_assets vne_copy_png_assets)
add_dependencies(vne_game vne_bake_assets)

# assets_src/ogg/ (M6, audio): audio_init() escanea este directorio con una ruta relativa
# a su propio CWD para armar el catalogo de musica (ver ADR de M6 en docs/DECISIONS.md),
# igual que vne_bake con los PNG arriba. Sonidos placeholder generados (tonos sinteticos,
# no assets de terceros): puerta_cierra.wav (sfx) y tema_a.wav/tema_b.wav (musica, para
# probar el crossfade con dos pistas distintas de verdad).
file(GLOB VNE_OGG_ASSETS ${CMAKE_SOURCE_DIR}/assets_src/ogg/*)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/.vne_ogg_assets_copied
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_src/ogg
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets_src/ogg
          ${CMAKE_CURRENT_BINARY_DIR}/assets_src/ogg
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/.vne_ogg_assets_copied
  DEPENDS ${VNE_OGG_ASSETS}
  COMMENT "Copiando assets_src/ogg/ (audio de prueba, tonos sinteticos) al directorio de build"
)
add_custom_target(vne_copy_ogg_assets
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/.vne_ogg_assets_copied
)
add_dependencies(vne_game vne_copy_ogg_assets)

# Fuentes de prueba (Noto Sans + Noto Sans JP, OFL, ADR-0017): se copian al directorio de
# build para que vne_game las encuentre con una ruta relativa a su propio CWD, igual que
# el atlas horneado. NotoSans.ttf (latina) se anadio en M10 para el idioma base del
# dialogo, separada de la CJK que solo hace falta "bajo demanda" (ver docs/DECISIONS.md).
foreach(font_file NotoSansJP-subset.ttf NotoSans-subset.ttf)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_src/ttf/${font_file}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_src/ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/assets_src/ttf/${font_file}
            ${CMAKE_CURRENT_BINARY_DIR}/assets_src/ttf/${font_file}
    DEPENDS ${CMAKE_SOURCE_DIR}/assets_src/ttf/${font_file}
    COMMENT "Copiando ${font_file} al directorio de build"
  )
endforeach()
add_custom_target(vne_copy_fonts DEPENDS
  ${CMAKE_CURRENT_BINARY_DIR}/assets_src/ttf/NotoSansJP-subset.ttf
  ${CMAKE_CURRENT_BINARY_DIR}/assets_src/ttf/NotoSans-subset.ttf)
add_dependencies(vne_game vne_copy_fonts)

# Guion de prueba de M3 (ADR pendiente, ver docs/DECISIONS.md): nombres placeholder
# obvios, no contenido de juego real (SPEC.md #6). Se compila a .vnc en el directorio de
# build, igual que el atlas y las fuentes.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo.vnc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_baked
  COMMAND vne_bake script
          ${CMAKE_SOURCE_DIR}/assets_src/scripts/demo.vns
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo.vnc
  DEPENDS vne_bake ${CMAKE_SOURCE_DIR}/assets_src/scripts/demo.vns
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.bin
  COMMENT "vne_bake: compilando demo.vns a demo.vnc"
)
add_custom_target(vne_bake_script DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo.vnc)
add_dependencies(vne_game vne_bake_script)

# Guion de prueba de M5 (docs/DECISIONS.md): 3 ramas y 2 finales (SPEC.md #12), nombres
# placeholder obvios igual que demo.vns.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_branching.vnc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_baked
  COMMAND vne_bake script
          ${CMAKE_SOURCE_DIR}/assets_src/scripts/demo_branching.vns
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_branching.vnc
  DEPENDS vne_bake ${CMAKE_SOURCE_DIR}/assets_src/scripts/demo_branching.vns
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.bin
  COMMENT "vne_bake: compilando demo_branching.vns a demo_branching.vnc"
)
add_custom_target(vne_bake_script_branching
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_branching.vnc)
add_dependencies(vne_game vne_bake_script_branching)

# Guion de prueba de M6 (audio): @bgm/@sfx/@stopbgm (docs/DECISIONS.md).
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_audio.vnc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_baked
  COMMAND vne_bake script
          ${CMAKE_SOURCE_DIR}/assets_src/scripts/demo_audio.vns
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_audio.vnc
  DEPENDS vne_bake ${CMAKE_SOURCE_DIR}/assets_src/scripts/demo_audio.vns
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.bin
  COMMENT "vne_bake: compilando demo_audio.vns a demo_audio.vnc"
)
add_custom_target(vne_bake_script_audio
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_audio.vnc)
add_dependencies(vne_game vne_bake_script_audio)

# Guion de prueba de M12 (transiciones): @transition fade/wipe/dissolve y @move.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_transitions.vnc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_baked
  COMMAND vne_bake script
          ${CMAKE_SOURCE_DIR}/assets_src/scripts/demo_transitions.vns
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_transitions.vnc
  DEPENDS vne_bake ${CMAKE_SOURCE_DIR}/assets_src/scripts/demo_transitions.vns
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.bin
  COMMENT "vne_bake: compilando demo_transitions.vns a demo_transitions.vnc"
)
add_custom_target(vne_bake_script_transitions
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_transitions.vnc)
add_dependencies(vne_game vne_bake_script_transitions)

# Mapa de prueba de M9 (TMX -> .vnm, ADR de M9 en docs/DECISIONS.md).
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_map.vnm
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_baked
  COMMAND vne_bake map
          ${CMAKE_SOURCE_DIR}/assets_src/maps/demo_map.tmx
          ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_map.vnm
  DEPENDS vne_bake ${CMAKE_SOURCE_DIR}/assets_src/maps/demo_map.tmx
  COMMENT "vne_bake: compilando demo_map.tmx a demo_map.vnm"
)
add_custom_target(vne_bake_map
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/demo_map.vnm)
add_dependencies(vne_game vne_bake_map)

# Catalogos de localizacion (M10, ADR-0046: horneados a binario). assets_src/locale/*.csv
# son el formato de autoria, ya en el repo (es.csv se regenera a mano con
# "vne_bake catalog-extract" cuando cambian los guiones, no automaticamente en cada build
# — evita que un build normal reescriba un archivo que un traductor puede estar editando).
foreach(locale es ja)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/${locale}.vnl
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets_baked
    COMMAND vne_bake catalog-compile
            ${CMAKE_SOURCE_DIR}/assets_src/locale/${locale}.csv
            ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/${locale}.vnl
    DEPENDS vne_bake ${CMAKE_SOURCE_DIR}/assets_src/locale/${locale}.csv
    COMMENT "vne_bake: compilando ${locale}.csv a ${locale}.vnl"
  )
  add_custom_target(vne_bake_catalog_${locale}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/${locale}.vnl)
  add_dependencies(vne_game vne_bake_catalog_${locale})
endforeach()

enable_testing()
add_subdirectory(tests)
