#include "text/layout.h"

#include <hb.h>

#include <cstring>

#include "base/log.h"
#include "gfx/gfx.h"
#include "gfx/texture.h"
#include "text/font_internal.h"
#include "text/glyph_cache.h"

namespace {

// Kinsoku basico (SPEC.md #7.2): estos caracteres no pueden empezar una linea...
constexpr u32 k_forbidden_start[] = {
    0x3002, 0x3001, 0x300D, 0x300F, 0xFF09,  // 。 、 」 』 ）
};
// ...y estos no pueden terminarla.
constexpr u32 k_forbidden_end[] = {
    0x300C, 0x300E, 0xFF08,  // 「 『 （
};

bool in_set(u32 cp, const u32* set, usize count) {
    for (usize i = 0; i < count; ++i) {
        if (set[i] == cp) return true;
    }
    return false;
}

bool is_forbidden_start(u32 cp) {
    return in_set(cp, k_forbidden_start, sizeof(k_forbidden_start) / sizeof(u32));
}
bool is_forbidden_end(u32 cp) {
    return in_set(cp, k_forbidden_end, sizeof(k_forbidden_end) / sizeof(u32));
}

bool is_cjk(u32 cp) {
    return (cp >= 0x3000 && cp <= 0x30FF)    // puntuacion CJK, hiragana, katakana
           || (cp >= 0x3400 && cp <= 0x9FFF)  // ideogramas unificados (+ extension A)
           || (cp >= 0xFF00 && cp <= 0xFFEF);  // formas de ancho completo
}

// Decodifica un codepoint UTF-8 desde s[i]; escribe en out_len los bytes consumidos.
// Entrada invalida se trata como un byte latin1 (nunca lee fuera de s.size()).
u32 utf8_decode(std::string_view s, usize i, usize* out_len) {
    u8 c0 = static_cast<u8>(s[i]);
    if (c0 < 0x80) {
        *out_len = 1;
        return c0;
    }
    if ((c0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
        *out_len = 2;
        return (static_cast<u32>(c0 & 0x1F) << 6) | (static_cast<u8>(s[i + 1]) & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
        *out_len = 3;
        return (static_cast<u32>(c0 & 0x0F) << 12) |
               (static_cast<u32>(static_cast<u8>(s[i + 1]) & 0x3F) << 6) |
               (static_cast<u8>(s[i + 2]) & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
        *out_len = 4;
        return (static_cast<u32>(c0 & 0x07) << 18) |
               (static_cast<u32>(static_cast<u8>(s[i + 1]) & 0x3F) << 12) |
               (static_cast<u32>(static_cast<u8>(s[i + 2]) & 0x3F) << 6) |
               (static_cast<u8>(s[i + 3]) & 0x3F);
    }
    *out_len = 1;
    return c0;
}

// --- Marcado inline: {b} {/b} {color=#rrggbb} {/color} {ruby=..} {/ruby} {w=n} {speed=n}
// {w=}/{speed=} no cambian la geometria, pero desde M12 si dejan rastro: se acumulan en
// el segmento que empiezan y acaban convertidos en TypewriterEvent (ver layout.h). Hasta
// M11 se reconocian y se tiraban.

struct Segment {
    std::string_view text;
    std::string_view ruby;
    u32              color;
    bool             bold;
    // Temporizacion del efecto de maquina de escribir, vigente al empezar este segmento.
    f32              pause_before     = 0.0f;  // segundos de {w=n} justo antes
    f32              speed_multiplier = 1.0f;  // ultimo {speed=n} visto
};

constexpr u32 k_max_segments = 256;

u32 parse_hex_color(std::string_view hex) {
    // "#rrggbb" -> 0xRRGGBBAAu (alpha fijo a opaco; el marcado no controla alpha).
    if (hex.size() != 7 || hex[0] != '#') {
        return 0xFFFFFFFFu;
    }
    auto nibble = [](char c) -> u32 {
        if (c >= '0' && c <= '9') return static_cast<u32>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<u32>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<u32>(c - 'A' + 10);
        return 0;
    };
    u32 r = (nibble(hex[1]) << 4) | nibble(hex[2]);
    u32 g = (nibble(hex[3]) << 4) | nibble(hex[4]);
    u32 b = (nibble(hex[5]) << 4) | nibble(hex[6]);
    return (r) | (g << 8) | (b << 16) | (0xFFu << 24);
}

// Sin std::stof (asigna y lanza) ni from_chars<f32> (no siempre disponible en todas las
// STL para flotantes): un parser minimo para "1.5" basta, el marcado no necesita mas.
// Devuelve false si la cadena no es un numero simple, para no aceptar {w=hola} en silencio.
bool parse_markup_float(std::string_view s, f32* out) {
    if (s.empty()) {
        return false;
    }
    f32   value    = 0.0f;
    f32   fraction = 0.0f;
    f32   scale    = 0.1f;
    bool  after_dot = false;
    bool  any_digit = false;
    for (char c : s) {
        if (c == '.') {
            if (after_dot) {
                return false;
            }
            after_dot = true;
            continue;
        }
        if (c < '0' || c > '9') {
            return false;
        }
        any_digit = true;
        if (after_dot) {
            fraction += static_cast<f32>(c - '0') * scale;
            scale *= 0.1f;
        } else {
            value = value * 10.0f + static_cast<f32>(c - '0');
        }
    }
    if (!any_digit) {
        return false;
    }
    *out = value + fraction;
    return true;
}

u32 parse_markup(std::string_view utf8, u32 base_color, Segment* out_segments) {
    u32   count     = 0;
    u32   color     = base_color;
    bool  bold      = false;
    usize run_start = 0;
    usize i         = 0;
    // Temporizacion pendiente de aplicar al siguiente segmento que se emita.
    f32 pending_pause = 0.0f;
    f32 speed         = 1.0f;

    auto push_segment = [&](std::string_view text, std::string_view ruby) {
        if (!text.empty() && count < k_max_segments) {
            out_segments[count] = Segment{text, ruby, color, bold, pending_pause, speed};
            // La pausa es un evento puntual: se consume en el primer segmento que la
            // sigue, no se repite en los demas. La velocidad, en cambio, es un estado
            // que se mantiene hasta el siguiente {speed=}.
            pending_pause = 0.0f;
            count += 1;
        }
    };

    while (i < utf8.size()) {
        if (utf8[i] != '{') {
            i += 1;
            continue;
        }
        push_segment(utf8.substr(run_start, i - run_start), {});

        usize close = utf8.find('}', i);
        if (close == std::string_view::npos) {
            // '{' sin cerrar: el resto se trata como texto literal.
            push_segment(utf8.substr(i), {});
            run_start = utf8.size();
            i         = utf8.size();
            break;
        }
        std::string_view tag = utf8.substr(i + 1, close - i - 1);

        if (tag == "b") {
            bold = true;
        } else if (tag == "/b") {
            bold = false;
        } else if (tag.rfind("color=", 0) == 0) {
            color = parse_hex_color(tag.substr(6));
        } else if (tag == "/color") {
            color = base_color;
        } else if (tag.rfind("ruby=", 0) == 0) {
            std::string_view ruby       = tag.substr(5);
            usize            base_start = close + 1;
            usize            ruby_close = utf8.find("{/ruby}", base_start);
            usize base_end = (ruby_close == std::string_view::npos) ? utf8.size() : ruby_close;
            push_segment(utf8.substr(base_start, base_end - base_start), ruby);
            i         = (ruby_close == std::string_view::npos) ? utf8.size() : ruby_close + 7;
            run_start = i;
            continue;
        } else if (tag.rfind("w=", 0) == 0) {
            f32 seconds = 0.0f;
            if (parse_markup_float(tag.substr(2), &seconds)) {
                // Se suman si hay varios seguidos: {w=0.2}{w=0.3} pausa 0.5s.
                pending_pause += seconds;
            }
        } else if (tag.rfind("speed=", 0) == 0) {
            f32 multiplier = 0.0f;
            if (parse_markup_float(tag.substr(6), &multiplier) && multiplier > 0.0f) {
                speed = multiplier;
            }
        }
        // {/ruby} suelto o una etiqueta desconocida: no producen segmento propio.

        run_start = close + 1;
        i         = close + 1;
    }
    push_segment(utf8.substr(run_start, i - run_start), {});
    return count;
}

// --- Chunking: unidades de wrap. Palabra+espacios en latin, un caracter por chunk en
// CJK (cualquier borde entre caracteres CJK es un punto de corte valido), un chunk unico
// por segmento de ruby (base+furigana no se separan entre lineas).

struct ShapedGlyph {
    u32 glyph_index;
    f32 x_advance, y_advance, x_offset, y_offset;  // pixeles
};

struct Chunk {
    std::string_view text;
    std::string_view ruby;
    u32              color           = 0xFFFFFFFFu;
    f32              width           = 0.0f;
    f32              ruby_width      = 0.0f;
    ShapedGlyph*     glyphs          = nullptr;
    u32              glyph_count     = 0;
    ShapedGlyph*     ruby_glyphs     = nullptr;
    u32              ruby_glyph_count = 0;
    bool             forbidden_start = false;
    bool             forbidden_end   = false;
    // Heredados del segmento del que salio este chunk (M12). La pausa solo va en el
    // PRIMER chunk del segmento: es un evento puntual, no una propiedad de cada trozo.
    f32              pause_before     = 0.0f;
    f32              speed_multiplier = 1.0f;
};

constexpr u32 k_max_chunks = 1024;

u32 last_codepoint(std::string_view s) {
    u32   cp  = 0;
    usize idx = 0;
    while (idx < s.size()) {
        usize len;
        cp = utf8_decode(s, idx, &len);
        idx += len;
    }
    return cp;
}

// Propaga la temporizacion del segmento a los chunks que acaba de producir (M12). Se hace
// al final y de una vez, y no en cada uno de los cinco sitios donde se construye un Chunk
// aqui dentro: menos ruido y, sobre todo, imposible olvidarse de uno.
void apply_segment_timing(const Segment& seg, Chunk* out_chunks, u32 first, u32 count) {
    for (u32 i = first; i < count; ++i) {
        out_chunks[i].speed_multiplier = seg.speed_multiplier;
    }
    if (count > first) {
        out_chunks[first].pause_before = seg.pause_before;  // puntual: solo el primero
    }
}

u32 chunk_segment(const Segment& seg, Chunk* out_chunks, u32 max_chunks, u32 count) {
    const u32 first_chunk = count;
    if (!seg.ruby.empty()) {
        if (count < max_chunks) {
            Chunk c;
            c.text  = seg.text;
            c.ruby  = seg.ruby;
            c.color = seg.color;
            usize len;
            u32 first_cp = seg.text.empty() ? 0 : utf8_decode(seg.text, 0, &len);
            c.forbidden_start   = is_forbidden_start(first_cp);
            c.forbidden_end     = is_forbidden_end(last_codepoint(seg.text));
            out_chunks[count++] = c;
        }
        apply_segment_timing(seg, out_chunks, first_chunk, count);
        return count;
    }

    usize i          = 0;
    usize latin_start = 0;
    bool  in_latin    = false;
    while (i < seg.text.size()) {
        usize len;
        u32   cp = utf8_decode(seg.text, i, &len);
        if (is_cjk(cp)) {
            if (in_latin && count < max_chunks) {
                Chunk c;
                c.text              = seg.text.substr(latin_start, i - latin_start);
                c.color             = seg.color;
                out_chunks[count++] = c;
            }
            in_latin = false;
            if (count < max_chunks) {
                Chunk c;
                c.text              = seg.text.substr(i, len);
                c.color             = seg.color;
                c.forbidden_start   = is_forbidden_start(cp);
                c.forbidden_end     = is_forbidden_end(cp);
                out_chunks[count++] = c;
            }
            i += len;
        } else {
            if (!in_latin) {
                in_latin    = true;
                latin_start = i;
            }
            i += len;
            if (cp == ' ') {
                while (i < seg.text.size() && seg.text[i] == ' ') {
                    i += 1;
                }
                if (count < max_chunks) {
                    Chunk c;
                    c.text              = seg.text.substr(latin_start, i - latin_start);
                    c.color             = seg.color;
                    out_chunks[count++] = c;
                }
                in_latin = false;
            }
        }
    }
    if (in_latin && count < max_chunks) {
        Chunk c;
        c.text              = seg.text.substr(latin_start, seg.text.size() - latin_start);
        c.color             = seg.color;
        out_chunks[count++] = c;
    }
    apply_segment_timing(seg, out_chunks, first_chunk, count);
    return count;
}

// Shaping con HarfBuzz de un unico chunk. `hb_buf` es un buffer de HarfBuzz reutilizado
// entre chunks (hb_buffer_reset() en vez de crear/destruir uno por chunk: un parrafo de
// 500 caracteres son ~100 chunks, y 100 malloc/free de mas por layout es justo lo que el
// criterio de <1ms de SPEC.md #12 no puede permitirse). Asigna en `arena`.
u32 shape_run(FontData* font_data, std::string_view text, hb_buffer_t* hb_buf, Arena* arena,
              ShapedGlyph** out_glyphs, f32* out_width) {
    *out_width = 0.0f;
    if (text.empty()) {
        *out_glyphs = nullptr;
        return 0;
    }

    hb_buffer_reset(hb_buf);
    hb_buffer_add_utf8(hb_buf, text.data(), static_cast<int>(text.size()), 0,
                        static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(hb_buf);
    hb_shape(font_data->hb_font, hb_buf, nullptr, 0);

    unsigned int          glyph_count = 0;
    hb_glyph_info_t*      infos       = hb_buffer_get_glyph_infos(hb_buf, &glyph_count);
    hb_glyph_position_t*  positions   = hb_buffer_get_glyph_positions(hb_buf, &glyph_count);

    ShapedGlyph* glyphs = arena_alloc_n<ShapedGlyph>(arena, glyph_count > 0 ? glyph_count : 1);
    f32          width  = 0.0f;
    for (unsigned int i = 0; i < glyph_count; ++i) {
        glyphs[i].glyph_index = infos[i].codepoint;  // ya es un indice de glifo tras hb_shape
        glyphs[i].x_advance   = static_cast<f32>(positions[i].x_advance) / 64.0f;
        glyphs[i].y_advance   = static_cast<f32>(positions[i].y_advance) / 64.0f;
        glyphs[i].x_offset    = static_cast<f32>(positions[i].x_offset) / 64.0f;
        glyphs[i].y_offset    = static_cast<f32>(positions[i].y_offset) / 64.0f;
        width += glyphs[i].x_advance;
    }

    *out_glyphs = glyphs;
    *out_width  = width;
    return glyph_count;
}

}  // namespace

bool text_is_kinsoku_forbidden_start(u32 codepoint) {
    return is_forbidden_start(codepoint);
}
bool text_is_kinsoku_forbidden_end(u32 codepoint) {
    return is_forbidden_end(codepoint);
}

u32 g_text_layout_call_count = 0;

TextLayout text_layout(FontHandle font, std::string_view utf8, f32 max_width, Arena* arena,
                        u32 base_color) {
    g_text_layout_call_count += 1;
    TextLayout result{};
    FontData*  font_data = font_resolve(font);
    if (font_data == nullptr) {
        log_error("text_layout: FontHandle invalido");
        return result;
    }

    // arena_alloc devuelve nullptr si la arena no tiene espacio (skill vne-memory-model):
    // no es un caso hipotetico, ya crasheo un test con una arena demasiado chica antes de
    // esta comprobacion. Un texto que no cabe no es fatal para el juego.
    Segment* segments = arena_alloc_n<Segment>(arena, k_max_segments);
    if (segments == nullptr) {
        log_error("text_layout: sin espacio en la arena para segments");
        return result;
    }
    u32 segment_count = parse_markup(utf8, base_color, segments);

    Chunk* chunks = arena_alloc_n<Chunk>(arena, k_max_chunks);
    if (chunks == nullptr) {
        log_error("text_layout: sin espacio en la arena para chunks");
        return result;
    }
    u32 chunk_count = 0;
    for (u32 s = 0; s < segment_count; ++s) {
        chunk_count = chunk_segment(segments[s], chunks, k_max_chunks, chunk_count);
    }

    hb_buffer_t* hb_buf = hb_buffer_create();
    for (u32 c = 0; c < chunk_count; ++c) {
        chunks[c].glyph_count = shape_run(font_data, chunks[c].text, hb_buf, arena,
                                           &chunks[c].glyphs, &chunks[c].width);
        if (!chunks[c].ruby.empty()) {
            chunks[c].ruby_glyph_count = shape_run(font_data, chunks[c].ruby, hb_buf, arena,
                                                    &chunks[c].ruby_glyphs, &chunks[c].ruby_width);
        }
    }
    hb_buffer_destroy(hb_buf);

    // Pase B (kinsoku basico, SPEC.md #7.2): decide donde empieza cada linea sin emitir
    // quads todavia, para poder mirar un chunk hacia adelante y hacia atras.
    // +1: en el peor caso (cada chunk fuerza un salto de linea) hacen falta chunk_count
    // entradas de ruptura mas la linea inicial en el indice 0.
    u32* line_starts = arena_alloc_n<u32>(arena, chunk_count + 1);
    if (line_starts == nullptr) {
        log_error("text_layout: sin espacio en la arena para line_starts");
        return result;
    }
    u32 line_count = 0;
    line_starts[line_count++] = 0;
    f32 pen_x                 = 0.0f;
    for (u32 c = 0; c < chunk_count; ++c) {
        f32 w = chunks[c].width > chunks[c].ruby_width ? chunks[c].width : chunks[c].ruby_width;
        bool would_overflow = (pen_x + w > max_width) && (pen_x > 0.0f);
        if (would_overflow && chunks[c].forbidden_start) {
            would_overflow = false;  // (a) no puede empezar linea: se queda en la actual
        }
        if (would_overflow) {
            u32 break_at = c;
            if (c > line_starts[line_count - 1] && chunks[c - 1].forbidden_end) {
                break_at = c - 1;  // (b) tampoco puede terminarla: se empuja a la siguiente
            }
            line_starts[line_count++] = break_at;
            pen_x                     = 0.0f;
            for (u32 k = break_at; k <= c; ++k) {
                f32 kw = chunks[k].width > chunks[k].ruby_width ? chunks[k].width
                                                                 : chunks[k].ruby_width;
                pen_x += kw;
            }
        } else {
            pen_x += w;
        }
    }

    // Pase C: emitir quads linea por linea.
    u32 max_quads = 1;
    for (u32 c = 0; c < chunk_count; ++c) {
        max_quads += chunks[c].glyph_count + chunks[c].ruby_glyph_count;
    }
    GlyphQuad* quads = arena_alloc_n<GlyphQuad>(arena, max_quads);
    if (quads == nullptr) {
        log_error("text_layout: sin espacio en la arena para quads");
        return result;
    }
    u32 quad_count = 0;

    // Eventos de maquina de escribir (M12). Como mucho uno por chunk, y solo se emite
    // cuando algo cambia de verdad respecto al chunk anterior.
    TypewriterEvent* events = arena_alloc_n<TypewriterEvent>(arena, chunk_count + 1);
    if (events == nullptr) {
        log_error("text_layout: sin espacio en la arena para events");
        return result;
    }
    u32 event_count      = 0;
    f32 current_speed    = 1.0f;

    constexpr f32 k_ruby_scale       = 0.5f;
    f32           ruby_extra_height  = font_data->line_height * k_ruby_scale;
    f32           layout_width       = 0.0f;
    f32           pen_y              = font_data->ascender;

    for (u32 line = 0; line < line_count; ++line) {
        u32 chunk_begin = line_starts[line];
        u32 chunk_end    = (line + 1 < line_count) ? line_starts[line + 1] : chunk_count;

        bool line_has_ruby = false;
        for (u32 c = chunk_begin; c < chunk_end; ++c) {
            if (chunks[c].ruby_glyph_count > 0) {
                line_has_ruby = true;
                break;
            }
        }
        if (line_has_ruby) {
            pen_y += ruby_extra_height;
        }

        f32 line_pen_x = 0.0f;
        for (u32 c = chunk_begin; c < chunk_end; ++c) {
            Chunk& chunk        = chunks[c];
            f32    chunk_start_x = line_pen_x;

            // Anclado en quad_count, que es exactamente el indice de glifo que cuenta
            // visible_glyphs al dibujar (incluidos los de furigana): asi VnMode puede
            // comparar los dos sin traducir nada.
            if (chunk.pause_before > 0.0f || chunk.speed_multiplier != current_speed) {
                events[event_count++] =
                    TypewriterEvent{quad_count, chunk.pause_before, chunk.speed_multiplier};
                current_speed = chunk.speed_multiplier;
            }

            for (u32 g = 0; g < chunk.glyph_count; ++g) {
                const ShapedGlyph& sg = chunk.glyphs[g];
                const GlyphInfo*   gi = glyph_cache_get(font, sg.glyph_index);
                if (gi != nullptr && gi->width > 0.0f) {
                    GlyphQuad& q = quads[quad_count++];
                    q.x          = line_pen_x + sg.x_offset + gi->bearing_x;
                    q.y          = pen_y - gi->bearing_y + sg.y_offset;
                    q.w          = gi->width;
                    q.h          = gi->height;
                    q.u0 = gi->u0;
                    q.v0 = gi->v0;
                    q.u1 = gi->u1;
                    q.v1 = gi->v1;
                    q.atlas_page = gi->atlas_page;
                    q.color      = chunk.color;
                    q.is_ruby    = false;
                }
                line_pen_x += sg.x_advance;
            }

            if (chunk.ruby_glyph_count > 0) {
                f32 ruby_pen_x = chunk_start_x + (chunk.width - chunk.ruby_width) * 0.5f;
                if (ruby_pen_x < chunk_start_x) {
                    ruby_pen_x = chunk_start_x;
                }
                for (u32 g = 0; g < chunk.ruby_glyph_count; ++g) {
                    const ShapedGlyph& sg = chunk.ruby_glyphs[g];
                    const GlyphInfo*   gi = glyph_cache_get(font, sg.glyph_index);
                    if (gi != nullptr && gi->width > 0.0f) {
                        GlyphQuad& q = quads[quad_count++];
                        q.x = ruby_pen_x + (sg.x_offset + gi->bearing_x) * k_ruby_scale;
                        q.y = pen_y - ruby_extra_height - gi->bearing_y * k_ruby_scale;
                        q.w = gi->width * k_ruby_scale;
                        q.h = gi->height * k_ruby_scale;
                        q.u0 = gi->u0;
                        q.v0 = gi->v0;
                        q.u1 = gi->u1;
                        q.v1 = gi->v1;
                        q.atlas_page = gi->atlas_page;
                        q.color      = chunk.color;
                        q.is_ruby    = true;
                    }
                    ruby_pen_x += sg.x_advance * k_ruby_scale;
                }
            }
        }
        if (line_pen_x > layout_width) {
            layout_width = line_pen_x;
        }
        pen_y += font_data->line_height;
    }

    // No se llama a glyph_cache_flush_dirty_pages() aqui a proposito: sg_update_image
    // solo admite una subida por imagen y por frame (validacion propia de sokol_gfx). Si
    // text_layout() se llama mas de una vez en el mismo frame (p. ej. al inicializar
    // varios cuadros de dialogo a la vez), una segunda subida de la misma pagina dentro
    // del mismo frame reventaria esa regla. El llamante (el bucle de frame, no este
    // modulo) debe invocar glyph_cache_flush_dirty_pages() una sola vez por frame, despues
    // de todos los text_layout() de ese frame y antes de gfx_flush() (ver
    // docs/DECISIONS.md, hito M2).

    result.quads      = quads;
    result.count      = quad_count;
    result.width      = layout_width;
    // pen_y avanzo una linea de mas tras la ultima iteracion; se descuenta para quedarse
    // con la altura real del bloque, hasta el descendente de la ultima linea.
    result.height = (pen_y - font_data->line_height) + font_data->descender;
    result.line_count  = line_count;
    result.events      = events;
    result.event_count = event_count;
    return result;
}

void text_draw(const TextLayout& l, f32 x, f32 y, u32 visible_glyphs) {
    u32 n = visible_glyphs < l.count ? visible_glyphs : l.count;
    for (u32 i = 0; i < n; ++i) {
        const GlyphQuad& q = l.quads[i];
        if (!q.atlas_page.valid()) {
            continue;  // glifos sin tinta (espacios): nada que dibujar
        }
        i32 page_w = 1, page_h = 1;
        texture_size(q.atlas_page, &page_w, &page_h);

        Sprite s{};
        s.tex   = q.atlas_page;
        s.src_x = q.u0 * static_cast<f32>(page_w);
        s.src_y = q.v0 * static_cast<f32>(page_h);
        s.src_w = (q.u1 - q.u0) * static_cast<f32>(page_w);
        s.src_h = (q.v1 - q.v0) * static_cast<f32>(page_h);
        s.dst_x = x + q.x;
        s.dst_y = y + q.y;
        s.dst_w = q.w;
        s.dst_h = q.h;
        s.color = q.color;
        s.layer = static_cast<u16>(GfxLayer::DialogueText);
        s.order = 0;
        gfx_draw_sprite(s);
    }
}
