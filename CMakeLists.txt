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
  if(VNE_ASAN_AVAILABLE)
    if(MSVC)
      target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/fsanitize=address>)
    else()
      target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
      target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
    endif()
  endif()
endfunction()

add_library(vne_base STATIC
  src/base/log.cpp
  src/base/arena.cpp
  src/base/heap_guard.cpp
  src/base/math.cpp
  src/platform/window.cpp
  src/platform/input.cpp
)
target_include_directories(vne_base PUBLIC src)
target_link_libraries(vne_base PUBLIC ${VNE_SDL3_TARGET} HandmadeMath)
vne_apply_project_flags(vne_base)

add_executable(vne_game src/main.cpp)
target_link_libraries(vne_game PRIVATE vne_base)
vne_apply_project_flags(vne_game)

enable_testing()
add_subdirectory(tests)
