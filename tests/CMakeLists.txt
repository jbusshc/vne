add_executable(vne_tests
  test_main.cpp
  test_arena.cpp
  test_pool.cpp
)
target_link_libraries(vne_tests PRIVATE vne_base doctest::doctest)
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
