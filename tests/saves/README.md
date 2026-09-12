# Partidas de ejemplo de cada versión histórica

El skill `vne-serializable-state` lo pide así: *"mantén una partida de ejemplo de cada versión
histórica en `tests/saves/` y un test que verifique que todas cargan"*. SPEC.md §12 lo hace
criterio de M14.

Hasta M14 los tests fabricaban cada `.vnsave` en memoria dentro del propio test. Eso prueba
que el código de migración funciona contra lo que el test *cree* que escribía un binario
viejo — que no es lo mismo que probarlo contra un archivo que un binario viejo escribió de
verdad. Estos archivos son fijos y se quedan aquí para siempre: **no se regeneran**. Si un
cambio los rompe, es el cambio el que está mal.

| Archivo | Versión | Qué lleva |
|---|---|---|
| `v1.vnsave` | 1 | Anterior a `map_id`/`player_x`/`player_y` (M9). `GameState` más corto. |
| `v2.vnsave` | 2 | Con mapa. Ids de actor/variable del esquema viejo. |
| `v3.vnsave` | 3 | Ids de actor ya inservibles (ADR-0062), variables aún por hash. |
| `v4.vnsave` | 4 | Variables ya por tabla de símbolos; backlog sin `key_hash`, 12 bytes por entrada. |

Los generó `vne_bake save-fixtures` (herramienta offline), que escribe cada formato byte a
byte tal y como lo habría escrito el binario de su época.
