# Fuentes

Las dos fuentes de este directorio son **subconjuntos**, no las originales. Las originales
pesaban 11,4 MB entre las dos (`NotoSansJP.ttf` sola eran 9,5 MB) y el proyecto usaba una
fracción minúscula de sus glifos; M13 las sustituyó por subconjuntos de **304 KB en total**.

| Archivo | Original | Tamaño |
|---|---|---|
| `NotoSans-subset.ttf` | [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic), OFL 1.1 | 2001 KB → 59 KB |
| `NotoSansJP-subset.ttf` | [Noto Sans JP](https://github.com/notofonts/noto-cjk), OFL 1.1 | 9365 KB → 245 KB |

Las licencias completas están aquí al lado (`OFL-NotoSans.txt`, `OFL-NotoSansJP.txt`) y
siguen aplicando a los subconjuntos: la OFL exige que una versión modificada se distribuya
entera bajo la misma licencia. El sufijo `-subset` está para que no haya duda de que son
versiones modificadas. `NotoSansJP` declara además el *Reserved Font Name* `'Source'` (viene
de Source Han Sans), que estos archivos no usan.

## Qué cubren, y qué pasa si falta un glifo

El subconjunto contiene ASCII imprimible, las letras acentuadas y signos del español, unos
pocos signos tipográficos (comillas, rayas, puntos suspensivos), **kana y puntuación CJK
completos**, y **cada codepoint que aparezca hoy en los guiones, catálogos y tests del
proyecto**.

Los kana entran en el conjunto base por una razón concreta: sin ellos, las funciones CJK del
motor (kinsoku en `src/text/layout.cpp`, furigana) quedan muertas aunque el código siga ahí.
La primera versión del subconjunto no los incluía y `test_kinsoku` y `test_ruby` empezaron a
fallar — que es justo la señal que se quería. Son unos 300 glifos.

Los **kanji** no están en el conjunto base: son miles y dependen del contenido, así que salen
de los archivos de texto que se le pasen al comando.

**Un carácter que no esté en el subconjunto se dibuja como un cuadrado** (`.notdef` de
FreeType). Es visible e inconfundible, nunca un fallo silencioso, pero conviene saberlo:
si añades diálogo en japonés de verdad, o cualquier idioma nuevo, **hay que regenerar el
subconjunto** o saldrán cuadrados.

## Cómo regenerar

Hace falta la fuente original, que ya no está en el repositorio: descárgala del enlace de la
tabla de arriba. Después:

```
vne_bake font <original.ttf> <salida-subset.ttf> [archivo_de_texto ...]
```

Cada archivo de texto que se le pase se recorre como UTF-8 y aporta sus codepoints. El
conjunto base (ASCII + español) se incluye siempre. El comando exacto con el que se
generaron estos dos:

```
vne_bake font NotoSansJP.ttf NotoSansJP-subset.ttf \
    assets_src/locale/es.csv assets_src/locale/ja.csv \
    assets_src/scripts/demo.vns assets_src/scripts/demo_branching.vns \
    assets_src/scripts/demo_audio.vns assets_src/scripts/demo_transitions.vns \
    tests/test_ruby.cpp tests/test_kinsoku.cpp
```

`vne_bake font` usa `hb-subset`, que es parte de HarfBuzz y ya estaba en la lista cerrada de
dependencias de `docs/SPEC.md` §3: no añade ninguna nueva.

Los dos `.cpp` de tests no son un descuido: contienen kanji literales (`漢字`) que el motor
tiene que poder dibujar para que esos tests signifiquen algo. Cualquier archivo UTF-8 vale.

**Aviso:** ten la fuente original a mano *antes* de sustituir la vieja. Subsetear no es
reversible — de un subconjunto no se puede sacar otro mayor. (Me pasó al implementarlo:
borré las originales, luego hubo que ampliar el conjunto con los kana, y hubo que
recuperarlas del historial de git.)
