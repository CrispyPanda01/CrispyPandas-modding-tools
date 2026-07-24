#include "hoi4_map.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WATER_COLOR 0xFF008CFFu
#define UNKNOWN_COLOR 0xFFFF00FFu
#define UNOWNED_COLOR 0xFF777B83u

typedef struct {
    char *text;
    size_t length;
    size_t pos;
} Lexer;

typedef enum { TOK_END, TOK_WORD, TOK_STRING, TOK_OPEN, TOK_CLOSE, TOK_EQUALS } TokenType;
typedef struct { TokenType type; char text[256]; } Token;

typedef struct {
    uint32_t key;
    uint32_t value;
} ColorSlot;

typedef struct {
    ColorSlot *slots;
    size_t capacity;
} ColorTable;

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const unsigned char *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *data;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)size + 1);
    if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    data[size] = '\0';
    if (length) *length = (size_t)size;
    return data;
}

static Token lexer_next(Lexer *lexer)
{
    Token token = {TOK_END, ""};
    size_t n = 0;
    for (;;) {
        while (lexer->pos < lexer->length && isspace((unsigned char)lexer->text[lexer->pos])) lexer->pos++;
        if (lexer->pos >= lexer->length) return token;
        if (lexer->text[lexer->pos] != '#') break;
        while (lexer->pos < lexer->length && lexer->text[lexer->pos] != '\n') lexer->pos++;
    }
    switch (lexer->text[lexer->pos]) {
    case '{': token.type = TOK_OPEN; token.text[0] = '{'; token.text[1] = 0; lexer->pos++; return token;
    case '}': token.type = TOK_CLOSE; token.text[0] = '}'; token.text[1] = 0; lexer->pos++; return token;
    case '=': token.type = TOK_EQUALS; token.text[0] = '='; token.text[1] = 0; lexer->pos++; return token;
    case '"':
        token.type = TOK_STRING;
        lexer->pos++;
        while (lexer->pos < lexer->length && lexer->text[lexer->pos] != '"' && n + 1 < sizeof(token.text)) {
            token.text[n++] = lexer->text[lexer->pos++];
        }
        if (lexer->pos < lexer->length) lexer->pos++;
        token.text[n] = 0;
        return token;
    default:
        token.type = TOK_WORD;
        while (lexer->pos < lexer->length
               && !isspace((unsigned char)lexer->text[lexer->pos])
               && !strchr("{}=#\"", lexer->text[lexer->pos])
               && n + 1 < sizeof(token.text)) {
            token.text[n++] = lexer->text[lexer->pos++];
        }
        token.text[n] = 0;
        return token;
    }
}

static bool token_is(const Token *token, const char *text)
{
    return token->type == TOK_WORD && _stricmp(token->text, text) == 0;
}

static uint32_t rgb_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void color_table_init(ColorTable *table, size_t capacity)
{
    table->capacity = capacity;
    table->slots = calloc(capacity, sizeof(*table->slots));
}

static void color_table_put(ColorTable *table, uint32_t key, uint32_t value)
{
    size_t i = (key * 2654435761u) & (table->capacity - 1);
    while (table->slots[i].key && table->slots[i].key != key) i = (i + 1) & (table->capacity - 1);
    table->slots[i].key = key ? key : 1;
    table->slots[i].value = value;
}

static uint32_t color_table_get(const ColorTable *table, uint32_t key)
{
    size_t i = (key * 2654435761u) & (table->capacity - 1);
    size_t start = i;
    while (table->slots[i].key) {
        if (table->slots[i].key == (key ? key : 1)) return table->slots[i].value;
        i = (i + 1) & (table->capacity - 1);
        if (i == start) break;
    }
    return 0;
}

static void set_error(Hoi4Map *map, const char *message, const char *path)
{
    snprintf(map->error, sizeof(map->error), "%s%s%s", message, path ? " : " : "", path ? path : "");
}

void hoi4_map_init(Hoi4Map *map)
{
    memset(map, 0, sizeof(*map));
}

void hoi4_map_free(Hoi4Map *map)
{
    size_t i;
    if (!map) return;
    if (map->states) {
        for (i = 0; i < HOI4_MAX_STATES; ++i) {
            if (map->states[i]) {
                free(map->states[i]->provinces);
                free(map->states[i]);
            }
        }
    }
    if (map->strategic_regions) {
        for (i = 0; i < HOI4_MAX_STATES; ++i) {
            if (map->strategic_regions[i]) {
                free(map->strategic_regions[i]->provinces);
                free(map->strategic_regions[i]);
            }
        }
    }
    free(map->states);
    free(map->strategic_regions);
    free(map->provinces);
    free(map->countries);
    free(map->province_at);
    free(map->state_at);
    free(map->pixels);
    free(map->base_pixels);
    hoi4_map_init(map);
}

static bool parse_definition(Hoi4Map *map, const char *path, ColorTable *colors)
{
    FILE *file = fopen(path, "rb");
    char line[1024];
    if (!file) {
        set_error(map, "Impossible d'ouvrir definition.csv", path);
        return false;
    }
    while (fgets(line, sizeof(line), file)) {
        int id, r, g, b;
        char type[32] = "";
        if (sscanf(line, "%d;%d;%d;%d;%31[^;]", &id, &r, &g, &b, type) == 5
            && id > 0 && id < HOI4_MAX_PROVINCES) {
            uint32_t key = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            map->provinces[id].id = id;
            map->provinces[id].r = (uint8_t)r;
            map->provinces[id].g = (uint8_t)g;
            map->provinces[id].b = (uint8_t)b;
            map->provinces[id].type = _stricmp(type, "land") == 0 ? HOI4_PROVINCE_LAND
                : _stricmp(type, "sea") == 0 ? HOI4_PROVINCE_SEA
                : _stricmp(type, "lake") == 0 ? HOI4_PROVINCE_LAKE : HOI4_PROVINCE_UNKNOWN;
            map->provinces[id].min_x = map->provinces[id].min_y = 0x7FFFFFFF;
            map->provinces[id].max_x = map->provinces[id].max_y = -1;
            color_table_put(colors, key, (uint32_t)id);
            map->loaded_province_count++;
        }
    }
    fclose(file);
    return true;
}

static Hoi4Country *country_get(Hoi4Map *map, const char *tag, bool create)
{
    size_t i;
    for (i = 0; i < map->country_count; ++i) {
        if (_stricmp(map->countries[i].tag, tag) == 0) return &map->countries[i];
    }
    if (!create) return NULL;
    if (map->country_count == map->country_capacity) {
        size_t capacity = map->country_capacity ? map->country_capacity * 2 : 256;
        Hoi4Country *grown;
        if (capacity > HOI4_MAX_COUNTRIES) return NULL;
        grown = realloc(map->countries, capacity * sizeof(*grown));
        if (!grown) return NULL;
        map->countries = grown;
        map->country_capacity = capacity;
    }
    memset(&map->countries[map->country_count], 0, sizeof(map->countries[0]));
    snprintf(map->countries[map->country_count].tag, sizeof(map->countries[0].tag), "%s", tag);
    return &map->countries[map->country_count++];
}

static void hsv_to_rgb(double h, double s, double v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double c = v * s;
    double hp = h * 6.0;
    double x = c * (1.0 - fabs(fmod(hp, 2.0) - 1.0));
    double rr = 0, gg = 0, bb = 0, m;
    if (hp < 1) { rr = c; gg = x; }
    else if (hp < 2) { rr = x; gg = c; }
    else if (hp < 3) { gg = c; bb = x; }
    else if (hp < 4) { gg = x; bb = c; }
    else if (hp < 5) { rr = x; bb = c; }
    else { rr = c; bb = x; }
    m = v - c;
    *r = (uint8_t)((rr + m) * 255.0 + 0.5);
    *g = (uint8_t)((gg + m) * 255.0 + 0.5);
    *b = (uint8_t)((bb + m) * 255.0 + 0.5);
}

static void parse_colors_file(Hoi4Map *map, const char *path)
{
    size_t length;
    char *text = read_file(path, &length);
    Lexer lex;
    Token token;
    char active_tag[8] = "";
    int depth = 0;
    if (!text) return;
    lex.text = text; lex.length = length; lex.pos = 0;
    while ((token = lexer_next(&lex)).type != TOK_END) {
        if (token.type == TOK_OPEN) { depth++; continue; }
        if (token.type == TOK_CLOSE) { depth--; if (depth <= 0) active_tag[0] = 0; continue; }
        if (token.type != TOK_WORD) continue;
        if (depth == 0 && strlen(token.text) >= 2 && strlen(token.text) <= 7) {
            Token eq = lexer_next(&lex);
            Token open = lexer_next(&lex);
            if (eq.type == TOK_EQUALS && open.type == TOK_OPEN) {
                snprintf(active_tag, sizeof(active_tag), "%.7s", token.text);
                depth = 1;
            }
            continue;
        }
        if (active_tag[0] && depth == 1 && _stricmp(token.text, "color") == 0) {
            Token eq = lexer_next(&lex);
            Token mode = lexer_next(&lex);
            Token open = lexer_next(&lex);
            Token a = lexer_next(&lex), b = lexer_next(&lex), c = lexer_next(&lex);
            Hoi4Country *country;
            if (eq.type != TOK_EQUALS || open.type != TOK_OPEN) continue;
            country = country_get(map, active_tag, true);
            if (!country) continue;
            if (_stricmp(mode.text, "hsv") == 0) {
                hsv_to_rgb(strtod(a.text, NULL), strtod(b.text, NULL), strtod(c.text, NULL),
                           &country->r, &country->g, &country->b);
            } else {
                country->r = (uint8_t)atoi(a.text);
                country->g = (uint8_t)atoi(b.text);
                country->b = (uint8_t)atoi(c.text);
            }
            (void)lexer_next(&lex);
        }
    }
    free(text);
}

static Hoi4State *parse_state_file(const char *path, const char *filename)
{
    size_t length;
    char *text = read_file(path, &length);
    Lexer lex;
    Token token;
    Hoi4State *state;
    int depth = 0;
    int history_depth = -1;
    bool in_provinces = false;
    int provinces_depth = -1;
    size_t capacity = 0;
    if (!text) return NULL;
    state = calloc(1, sizeof(*state));
    if (!state) { free(text); return NULL; }
    state->min_x = state->min_y = 0x7FFFFFFF;
    state->max_x = state->max_y = -1;
    snprintf(state->source, sizeof(state->source), "%s", path);
    snprintf(state->name, sizeof(state->name), "%s", filename);
    {
        char *dot = strrchr(state->name, '.');
        if (dot) *dot = 0;
    }
    lex.text = text; lex.length = length; lex.pos = 0;
    while ((token = lexer_next(&lex)).type != TOK_END) {
        if (token.type == TOK_OPEN) { depth++; continue; }
        if (token.type == TOK_CLOSE) {
            if (depth == provinces_depth) { in_provinces = false; provinces_depth = -1; }
            if (depth == history_depth) history_depth = -1;
            depth--;
            continue;
        }
        if (in_provinces && token.type == TOK_WORD) {
            int id = atoi(token.text);
            if (id > 0 && id < HOI4_MAX_PROVINCES) {
                if (state->province_count == capacity) {
                    size_t new_capacity = capacity ? capacity * 2 : 16;
                    int *grown = realloc(state->provinces, new_capacity * sizeof(*grown));
                    if (!grown) break;
                    state->provinces = grown;
                    capacity = new_capacity;
                }
                state->provinces[state->province_count++] = id;
            }
            continue;
        }
        if (token.type != TOK_WORD) continue;
        if (token_is(&token, "id")) {
            Token eq = lexer_next(&lex), value = lexer_next(&lex);
            if (eq.type == TOK_EQUALS) state->id = atoi(value.text);
        } else if (token_is(&token, "name")) {
            Token eq = lexer_next(&lex), value = lexer_next(&lex);
            if (eq.type == TOK_EQUALS && (value.type == TOK_STRING || value.type == TOK_WORD)
                && strncmp(value.text, "STATE_", 6) != 0) {
                snprintf(state->name, sizeof(state->name), "%.159s", value.text);
            }
        } else if (token_is(&token, "history")) {
            Token eq = lexer_next(&lex), open = lexer_next(&lex);
            if (eq.type == TOK_EQUALS && open.type == TOK_OPEN) { depth++; history_depth = depth; }
        } else if (token_is(&token, "owner") && history_depth > 0 && depth == history_depth) {
            Token eq = lexer_next(&lex), value = lexer_next(&lex);
            if (eq.type == TOK_EQUALS) snprintf(state->owner, sizeof(state->owner), "%.7s", value.text);
        } else if (token_is(&token, "provinces")) {
            Token eq = lexer_next(&lex), open = lexer_next(&lex);
            if (eq.type == TOK_EQUALS && open.type == TOK_OPEN) {
                depth++;
                in_provinces = true;
                provinces_depth = depth;
            }
        }
    }
    free(text);
    if (state->id <= 0 || state->id >= HOI4_MAX_STATES || state->province_count == 0) {
        free(state->provinces);
        free(state);
        return NULL;
    }
    return state;
}

typedef struct { Hoi4Map *map; } StateVisitor;

static bool state_visitor(const char *path, const char *name, void *user)
{
    StateVisitor *context = user;
    Hoi4State *state = parse_state_file(path, name);
    if (state) {
        if (context->map->states[state->id]) {
            free(context->map->states[state->id]->provinces);
            free(context->map->states[state->id]);
        } else {
            context->map->loaded_state_count++;
        }
        context->map->states[state->id] = state;
    }
    return true;
}

static Hoi4StrategicRegion *parse_strategic_region_file(const char *path, const char *filename)
{
    size_t length;
    char *text = read_file(path, &length);
    Lexer lex;
    Token token;
    Hoi4StrategicRegion *region;
    bool in_provinces = false;
    int depth = 0, provinces_depth = -1;
    size_t capacity = 0;
    if (!text) return NULL;
    region = calloc(1, sizeof(*region));
    if (!region) { free(text); return NULL; }
    region->min_x = region->min_y = 0x7FFFFFFF;
    region->max_x = region->max_y = -1;
    snprintf(region->source, sizeof(region->source), "%s", path);
    snprintf(region->name, sizeof(region->name), "%s", filename);
    {
        char *dot = strrchr(region->name, '.');
        if (dot) *dot = 0;
    }
    lex.text = text; lex.length = length; lex.pos = 0;
    while ((token = lexer_next(&lex)).type != TOK_END) {
        if (token.type == TOK_OPEN) { depth++; continue; }
        if (token.type == TOK_CLOSE) {
            if (depth == provinces_depth) { in_provinces = false; provinces_depth = -1; }
            depth--;
            continue;
        }
        if (in_provinces && token.type == TOK_WORD) {
            int id = atoi(token.text);
            if (id > 0 && id < HOI4_MAX_PROVINCES) {
                if (region->province_count == capacity) {
                    size_t next_capacity = capacity ? capacity * 2 : 32;
                    int *grown = realloc(region->provinces, next_capacity * sizeof(*grown));
                    if (!grown) break;
                    region->provinces = grown;
                    capacity = next_capacity;
                }
                region->provinces[region->province_count++] = id;
            }
            continue;
        }
        if (token.type != TOK_WORD) continue;
        if (token_is(&token, "id")) {
            Token eq = lexer_next(&lex), value = lexer_next(&lex);
            if (eq.type == TOK_EQUALS) region->id = atoi(value.text);
        } else if (token_is(&token, "name")) {
            Token eq = lexer_next(&lex), value = lexer_next(&lex);
            if (eq.type == TOK_EQUALS && (value.type == TOK_STRING || value.type == TOK_WORD)
                && strncmp(value.text, "STRATEGICREGION_", 16) != 0) {
                snprintf(region->name, sizeof(region->name), "%.159s", value.text);
            }
        } else if (token_is(&token, "provinces")) {
            Token eq = lexer_next(&lex), open = lexer_next(&lex);
            if (eq.type == TOK_EQUALS && open.type == TOK_OPEN) {
                depth++;
                in_provinces = true;
                provinces_depth = depth;
            }
        }
    }
    free(text);
    if (region->id <= 0 || region->id >= HOI4_MAX_STATES || region->province_count == 0) {
        free(region->provinces);
        free(region);
        return NULL;
    }
    return region;
}

typedef struct { Hoi4Map *map; } StrategicVisitor;

static bool strategic_visitor(const char *path, const char *name, void *user)
{
    StrategicVisitor *context = user;
    Hoi4StrategicRegion *region = parse_strategic_region_file(path, name);
    if (region) {
        if (context->map->strategic_regions[region->id]) {
            free(context->map->strategic_regions[region->id]->provinces);
            free(context->map->strategic_regions[region->id]);
        } else {
            context->map->loaded_strategic_region_count++;
        }
        context->map->strategic_regions[region->id] = region;
    }
    return true;
}

static const char *layered_path(char *buffer, size_t size, const char *game, const char *mod, const char *relative)
{
    if (mod && mod[0] && cp_path_join(buffer, size, mod, relative) && cp_path_exists(buffer)) return buffer;
    if (cp_path_join(buffer, size, game, relative) && cp_path_exists(buffer)) return buffer;
    return NULL;
}

static uint32_t state_color(const Hoi4Map *map, uint16_t state_id)
{
    const Hoi4State *state;
    Hoi4Country *country;
    uint32_t hash;
    if (!state_id) return WATER_COLOR;
    if (state_id == UINT16_MAX || !(state = map->states[state_id])) return UNKNOWN_COLOR;
    if (!state->owner[0]) return UNOWNED_COLOR;
    country = country_get((Hoi4Map *)map, state->owner, false);
    if (country && (country->r || country->g || country->b)) return rgb_pixel(country->r, country->g, country->b);
    hash = 2166136261u;
    for (const char *p = state->owner; *p; ++p) hash = (hash ^ (unsigned char)*p) * 16777619u;
    return rgb_pixel((uint8_t)(70 + hash % 150), (uint8_t)(70 + (hash >> 8) % 150),
                     (uint8_t)(70 + (hash >> 16) % 150));
}

static uint16_t pixel_entity(const Hoi4Map *map, Hoi4ViewMode mode, size_t index)
{
    uint16_t province = map->province_at[index];
    if (mode == HOI4_VIEW_PROVINCES) return province;
    if (mode == HOI4_VIEW_STRATEGIC_REGIONS) {
        return province && province != UINT16_MAX ? map->provinces[province].strategic_region_id : 0;
    }
    return map->state_at[index];
}

static bool same_country(const Hoi4Map *map, uint16_t a, uint16_t b)
{
    const Hoi4State *sa;
    const Hoi4State *sb;
    if (a == b) return true;
    if (!a || !b || a == UINT16_MAX || b == UINT16_MAX) return false;
    sa = map->states[a];
    sb = map->states[b];
    if (!sa || !sb) return false;
    return _stricmp(sa->owner, sb->owner) == 0;
}

static uint32_t shade(uint32_t color, float factor)
{
    return rgb_pixel((uint8_t)(((color >> 16) & 255) * factor),
                     (uint8_t)(((color >> 8) & 255) * factor),
                     (uint8_t)((color & 255) * factor));
}

void hoi4_map_render_mode(Hoi4Map *map, Hoi4ViewMode mode)
{
    int x, y;
    if (!map || !map->province_at || !map->state_at) return;
    map->view_mode = mode;
    for (y = 0; y < map->height; ++y) {
        for (x = 0; x < map->width; ++x) {
            size_t index = (size_t)y * map->width + x;
            uint16_t state = map->state_at[index];
            uint16_t entity = pixel_entity(map, mode, index);
            uint32_t color = state_color(map, state);
            bool country_edge = false;
            bool state_edge = false;
            bool mode_edge = false;
#define CHECK_NEIGHBOR(neighbor_index) do { \
                uint16_t neighbor_state = map->state_at[(neighbor_index)]; \
                if (!same_country(map, state, neighbor_state)) country_edge = true; \
                if (neighbor_state != state) state_edge = true; \
                if (pixel_entity(map, mode, (neighbor_index)) != entity) mode_edge = true; \
            } while (0)
            if (x > 0) CHECK_NEIGHBOR(index - 1);
            if (x + 1 < map->width) CHECK_NEIGHBOR(index + 1);
            if (y > 0) CHECK_NEIGHBOR(index - map->width);
            if (y + 1 < map->height) CHECK_NEIGHBOR(index + map->width);
#undef CHECK_NEIGHBOR
            if (country_edge) color = shade(color, 0.30f);
            else if (mode != HOI4_VIEW_STATES && state_edge) color = shade(color, 0.45f);
            else if (mode_edge && entity) color = shade(color, 0.62f);
            map->base_pixels[index] = map->pixels[index] = color;
        }
    }
}

static void assign_provinces_to_entities(Hoi4Map *map)
{
    size_t s, p;
    for (s = 1; s < HOI4_MAX_STATES; ++s) {
        Hoi4State *state = map->states[s];
        if (!state) continue;
        for (p = 0; p < state->province_count; ++p) {
            int province = state->provinces[p];
            if (province > 0 && province < HOI4_MAX_PROVINCES)
                map->provinces[province].state_id = (uint16_t)s;
        }
    }
    for (s = 1; s < HOI4_MAX_STATES; ++s) {
        Hoi4StrategicRegion *region = map->strategic_regions[s];
        if (!region) continue;
        for (p = 0; p < region->province_count; ++p) {
            int province = region->provinces[p];
            if (province > 0 && province < HOI4_MAX_PROVINCES)
                map->provinces[province].strategic_region_id = (uint16_t)s;
        }
    }
}

static bool load_bmp_fast(Hoi4Map *map, const char *path, const ColorTable *colors)
{
    size_t length;
    unsigned char *data = (unsigned char *)read_file(path, &length);
    uint32_t offset, compression;
    int width, raw_height, height, row_size, x, y;
    uint16_t bpp;
    if (!data || length < 54 || data[0] != 'B' || data[1] != 'M') {
        free(data); set_error(map, "BMP de provinces invalide", path); return false;
    }
    offset = read_u32(data + 10); width = (int)read_u32(data + 18);
    raw_height = (int)read_u32(data + 22); height = raw_height < 0 ? -raw_height : raw_height;
    bpp = read_u16(data + 28); compression = read_u32(data + 30);
    row_size = ((width * 3 + 3) / 4) * 4;
    if (width <= 0 || height <= 0 || bpp != 24 || compression != 0
        || offset + (size_t)row_size * (size_t)height > length) {
        free(data); set_error(map, "Le BMP doit être non compressé en 24 bits", path); return false;
    }
    map->width = width; map->height = height;
    map->province_at = calloc((size_t)width * (size_t)height, sizeof(*map->province_at));
    map->state_at = calloc((size_t)width * (size_t)height, sizeof(*map->state_at));
    map->pixels = malloc((size_t)width * (size_t)height * sizeof(*map->pixels));
    map->base_pixels = malloc((size_t)width * (size_t)height * sizeof(*map->base_pixels));
    if (!map->province_at || !map->state_at || !map->pixels || !map->base_pixels) {
        free(data); set_error(map, "Mémoire insuffisante pour la carte", NULL); return false;
    }
    for (y = 0; y < height; ++y) {
        int source_y = raw_height > 0 ? height - 1 - y : y;
        const unsigned char *row = data + offset + (size_t)source_y * (size_t)row_size;
        for (x = 0; x < width; ++x) {
            uint32_t key = ((uint32_t)row[x * 3 + 2] << 16)
                         | ((uint32_t)row[x * 3 + 1] << 8) | row[x * 3];
            uint32_t province = color_table_get(colors, key);
            size_t index = (size_t)y * width + x;
            if (province) {
                Hoi4Province *province_data = &map->provinces[province];
                uint16_t state_id = province_data->state_id;
                map->province_at[index] = (uint16_t)province;
                if (x < province_data->min_x) province_data->min_x = x;
                if (x > province_data->max_x) province_data->max_x = x;
                if (y < province_data->min_y) province_data->min_y = y;
                if (y > province_data->max_y) province_data->max_y = y;
                if (province_data->type == HOI4_PROVINCE_LAND)
                    map->state_at[index] = state_id ? state_id : UINT16_MAX;
                if (state_id && map->states[state_id]) {
                    Hoi4State *state = map->states[state_id];
                    if (x < state->min_x) state->min_x = x;
                    if (x > state->max_x) state->max_x = x;
                    if (y < state->min_y) state->min_y = y;
                    if (y > state->max_y) state->max_y = y;
                }
                if (province_data->strategic_region_id
                    && map->strategic_regions[province_data->strategic_region_id]) {
                    Hoi4StrategicRegion *region =
                        map->strategic_regions[province_data->strategic_region_id];
                    if (x < region->min_x) region->min_x = x;
                    if (x > region->max_x) region->max_x = x;
                    if (y < region->min_y) region->min_y = y;
                    if (y > region->max_y) region->max_y = y;
                }
            } else if (!province) {
                map->province_at[index] = UINT16_MAX;
                map->state_at[index] = UINT16_MAX;
            }
        }
    }
    free(data);
    return true;
}

bool hoi4_map_load(Hoi4Map *map, const char *game_root, const char *mod_root)
{
    char path[CP_PATH_MAX];
    const char *selected;
    ColorTable colors;
    StateVisitor visitor;
    StrategicVisitor strategic_visitor_context;
    hoi4_map_free(map);
    snprintf(map->game_root, sizeof(map->game_root), "%s", game_root ? game_root : "");
    snprintf(map->mod_root, sizeof(map->mod_root), "%s", mod_root ? mod_root : "");
    map->states = calloc(HOI4_MAX_STATES, sizeof(*map->states));
    map->strategic_regions = calloc(HOI4_MAX_STATES, sizeof(*map->strategic_regions));
    map->provinces = calloc(HOI4_MAX_PROVINCES, sizeof(*map->provinces));
    if (!map->states || !map->strategic_regions || !map->provinces) {
        set_error(map, "Mémoire insuffisante", NULL); return false;
    }
    color_table_init(&colors, 131072);
    if (!colors.slots) { set_error(map, "Mémoire insuffisante", NULL); return false; }

    selected = layered_path(path, sizeof(path), game_root, mod_root, "map\\definition.csv");
    if (!selected || !parse_definition(map, selected, &colors)) { free(colors.slots); return false; }

    if (cp_path_join(path, sizeof(path), game_root, "common\\countries\\colors.txt")) parse_colors_file(map, path);
    if (mod_root && mod_root[0] && cp_path_join(path, sizeof(path), mod_root, "common\\countries\\colors.txt"))
        parse_colors_file(map, path);

    visitor.map = map;
    if (cp_path_join(path, sizeof(path), game_root, "history\\states")) cp_visit_txt_files(path, state_visitor, &visitor);
    if (mod_root && mod_root[0] && cp_path_join(path, sizeof(path), mod_root, "history\\states"))
        cp_visit_txt_files(path, state_visitor, &visitor);
    if (map->loaded_state_count == 0) {
        free(colors.slots); set_error(map, "Aucun état HOI4 n'a été trouvé", NULL); return false;
    }
    strategic_visitor_context.map = map;
    if (cp_path_join(path, sizeof(path), game_root, "map\\strategicregions"))
        cp_visit_txt_files(path, strategic_visitor, &strategic_visitor_context);
    if (mod_root && mod_root[0] && cp_path_join(path, sizeof(path), mod_root, "map\\strategicregions"))
        cp_visit_txt_files(path, strategic_visitor, &strategic_visitor_context);
    assign_provinces_to_entities(map);
    selected = layered_path(path, sizeof(path), game_root, mod_root, "map\\provinces.bmp");
    if (!selected || !load_bmp_fast(map, selected, &colors)) { free(colors.slots); return false; }
    free(colors.slots);
    hoi4_map_render_mode(map, HOI4_VIEW_STATES);
    return true;
}

const Hoi4State *hoi4_map_state(const Hoi4Map *map, int id)
{
    return map && id > 0 && id < HOI4_MAX_STATES ? map->states[id] : NULL;
}

const Hoi4Province *hoi4_map_province(const Hoi4Map *map, int id)
{
    return map && id > 0 && id < HOI4_MAX_PROVINCES && map->provinces[id].id
        ? &map->provinces[id] : NULL;
}

const Hoi4StrategicRegion *hoi4_map_strategic_region(const Hoi4Map *map, int id)
{
    return map && id > 0 && id < HOI4_MAX_STATES ? map->strategic_regions[id] : NULL;
}

int hoi4_map_entity_at(const Hoi4Map *map, Hoi4ViewMode mode, int x, int y)
{
    size_t index;
    uint16_t id;
    if (!map || !map->province_at || x < 0 || y < 0 || x >= map->width || y >= map->height) return 0;
    index = (size_t)y * map->width + x;
    id = pixel_entity(map, mode, index);
    return id == UINT16_MAX ? 0 : id;
}

bool hoi4_map_entity_bounds(const Hoi4Map *map, Hoi4ViewMode mode, int id, Hoi4Bounds *bounds)
{
    if (!map || !bounds || id <= 0) return false;
    if (mode == HOI4_VIEW_PROVINCES) {
        const Hoi4Province *province = hoi4_map_province(map, id);
        if (!province || province->max_x < 0) return false;
        bounds->min_x = province->min_x; bounds->min_y = province->min_y;
        bounds->max_x = province->max_x; bounds->max_y = province->max_y;
    } else if (mode == HOI4_VIEW_STRATEGIC_REGIONS) {
        const Hoi4StrategicRegion *region = hoi4_map_strategic_region(map, id);
        if (!region || region->max_x < 0) return false;
        bounds->min_x = region->min_x; bounds->min_y = region->min_y;
        bounds->max_x = region->max_x; bounds->max_y = region->max_y;
    } else {
        const Hoi4State *state = hoi4_map_state(map, id);
        if (!state || state->max_x < 0) return false;
        bounds->min_x = state->min_x; bounds->min_y = state->min_y;
        bounds->max_x = state->max_x; bounds->max_y = state->max_y;
    }
    return true;
}

void hoi4_map_paint_entity(Hoi4Map *map, Hoi4ViewMode mode, int id,
                           bool selected, bool hovered)
{
    Hoi4Bounds bounds;
    int x, y;
    if (!hoi4_map_entity_bounds(map, mode, id, &bounds)) return;
    for (y = bounds.min_y; y <= bounds.max_y; ++y) {
        for (x = bounds.min_x; x <= bounds.max_x; ++x) {
            size_t index = (size_t)y * map->width + x;
            if (pixel_entity(map, mode, index) == id) {
                uint32_t c = map->base_pixels[index];
                if (selected) {
                    uint8_t r = (uint8_t)((((c >> 16) & 255) + 255) / 2);
                    uint8_t g = (uint8_t)((((c >> 8) & 255) + 190) / 2);
                    uint8_t b = (uint8_t)((c & 255) * 0.42);
                    c = rgb_pixel(r, g, b);
                }
                if (hovered) {
                    uint8_t r = (uint8_t)((((c >> 16) & 255) + 255) / 2);
                    uint8_t g = (uint8_t)((((c >> 8) & 255) + 255) / 2);
                    uint8_t b = (uint8_t)(((c & 255) + 96) > 255 ? 255 : (c & 255) + 96);
                    c = rgb_pixel(r, g, b);
                }
                map->pixels[index] = c;
            }
        }
    }
}

void hoi4_map_set_hover(Hoi4Map *map, Hoi4ViewMode mode, int previous, int current)
{
    if (!map || previous == current) return;
    hoi4_map_paint_entity(map, mode, previous, false, false);
    hoi4_map_paint_entity(map, mode, current, false, true);
}
