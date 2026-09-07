configure_file(test_config.h.in test_config.h)

add_executable(vne_tests
  test_main.cpp
  test_arena.cpp
  test_pool.cpp
  test_radix_sort.cpp
  test_letterbox.cpp
  test_word_wrap.cpp
  test_kinsoku.cpp
  test_markup.cpp
  test_ruby.cpp
  test_layout_perf.cpp
  test_typewriter.cpp
  test_glyph_cache.cpp
)
target_link_libraries(vne_tests PRIVATE vne_base doctest::doctest)
target_include_directories(vne_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
vne_apply_project_flags(vne_tests)

# El proyecto compila sin excepciones (SPEC.md #4); doctest soporta ese modo con este
# define, que sustituye CHECK/REQUIRE por variantes que no dependen de throw/catch.
target_compile_definitions(vne_tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

if(MSVC)
  # doctest.h especializa std::tuple internamente; el STL de MSVC mas reciente lo marca
  # como advertencia (C5285). Es codigo de terceros que llega a nuestra unidad de
  # traduccion via #include, no un problema de nuestro codigo: se silencia solo aqui.
  target_compile_options(vne_tests PRIVATE /wd5285)
endif()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(vne_tests)
