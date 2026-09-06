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

# stb_image.h esta en el stack cerrado (SPEC.md #3) para cuando tools/bake empaquete PNGs
# reales de assets_src/ (ADR-0011), pero M1 todavia no lo necesita: el atlas de prueba es
# procedural. Se anade cuando haya assets reales que leer, no antes.

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
  src/base/math.cpp
  src/base/radix_sort.cpp
  src/platform/window.cpp
  src/platform/input.cpp
  src/gfx/gfx.cpp
  src/gfx/texture.cpp
  ${VNE_GFX_BACKEND_SOURCES}
)
target_include_directories(vne_base PUBLIC src)
target_link_libraries(vne_base
  PUBLIC ${VNE_SDL3_TARGET} HandmadeMath
  PRIVATE sokol_gfx qoi ${VNE_GFX_BACKEND_LIBS}
)
target_compile_definitions(vne_base PRIVATE ${VNE_GFX_BACKEND_DEFINE})
# El backend de sokol_gfx solo debe estar activo en la unidad de traduccion que hace
# SOKOL_IMPL (gfx.cpp); definirlo a nivel de target es simple y no da falsos positivos
# porque ninguna otra .cpp de vne_base incluye sokol_gfx.h con SOKOL_IMPL.
set_source_files_properties(src/gfx/gfx.cpp PROPERTIES COMPILE_DEFINITIONS ${VNE_GFX_SOKOL_BACKEND_DEFINE})
vne_apply_project_flags(vne_base)

add_executable(vne_game src/main.cpp)
target_link_libraries(vne_game PRIVATE vne_base)
vne_apply_project_flags(vne_game)

# Herramienta offline: no enlaza vne_base (SDL3/sokol/gfx) porque no abre ventana ni
# dibuja nada, solo genera y codifica bytes. Solo toma prestado base/log.cpp de vne_base.
add_executable(vne_bake tools/bake/main.cpp src/base/log.cpp)
target_include_directories(vne_bake PRIVATE src)
target_link_libraries(vne_bake PRIVATE qoi)
vne_apply_project_flags(vne_bake)

# Un checkout limpio mas un build debe producir un juego ejecutable sin pasos manuales
# (skill vne-build-verify): vne_game depende del atlas de prueba horneado por vne_bake en
# el propio directorio de build, no de que alguien se acuerde de ejecutarlo a mano.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.qoi
         ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.bin
  COMMAND vne_bake
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS vne_bake
  COMMENT "vne_bake: generando atlas de prueba (ADR-0011)"
)
add_custom_target(vne_bake_assets
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets_baked/atlas_00.qoi
)
add_dependencies(vne_game vne_bake_assets)

enable_testing()
add_subdirectory(tests)
