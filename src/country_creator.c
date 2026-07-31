#include "country_creator.h"

#include <windows.h>

#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

typedef struct {
    char path[CP_PATH_MAX];
    char temporary[CP_PATH_MAX];
    uint8_t *new_data;
    size_t new_size;
    uint8_t *old_data;
    size_t old_size;
    bool existed;
    bool committed;
} CountryChange;

typedef struct {
    CountryChange items[8];
    size_t count;
} CountryChanges;

static const char *culture_labels[COUNTRY_CULTURE_COUNT] = {
    "Europe occidentale", "Europe orientale", "Asie", "Afrique",
    "Amérique du Sud", "Moyen-Orient", "Commonwealth"
};

static const char *culture_files[COUNTRY_CULTURE_COUNT] = {
    "_western_european.txt", "_eastern_european.txt", "_asian.txt",
    "_african.txt", "_southamerican.txt", "_middle_eastern.txt",
    "_commonwealth.txt"
};

static const char *ideologies[COUNTRY_IDEOLOGY_COUNT] = {
    "neutrality", "democratic", "fascism", "communism"
};

static bool buffer_reserve(Buffer *buffer, size_t extra)
{
    size_t required = buffer->length + extra + 1;
    size_t capacity;
    char *grown;
    if (required <= buffer->capacity) return true;
    capacity = buffer->capacity ? buffer->capacity : 512;
    while (capacity < required) capacity *= 2;
    grown = realloc(buffer->data, capacity);
    if (!grown) return false;
    buffer->data = grown;
    buffer->capacity = capacity;
    return true;
}

static bool buffer_add_n(Buffer *buffer, const char *text, size_t length)
{
    if (!buffer_reserve(buffer, length)) return false;
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool buffer_add(Buffer *buffer, const char *text)
{
    return buffer_add_n(buffer, text, strlen(text));
}

static bool buffer_printf(Buffer *buffer, const char *format, ...)
{
    va_list args, copy;
    int length;
    va_start(args, format);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0 || !buffer_reserve(buffer, (size_t)length)) {
        va_end(args);
        return false;
    }
    vsnprintf(buffer->data + buffer->length,
              buffer->capacity - buffer->length, format, args);
    va_end(args);
    buffer->length += (size_t)length;
    return true;
}

static uint8_t *read_binary(const char *path, size_t *size)
{
    FILE *file;
    long length;
    uint8_t *data;
    *size = 0;
    file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)length + 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (length && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    data[length] = 0;
    *size = (size_t)length;
    return data;
}

static bool path_is_file(const char *path)
{
    return cp_path_exists(path) && !cp_path_is_dir(path);
}

static bool mkdir_parents(const char *path)
{
    char copy[CP_PATH_MAX];
    size_t i;
    snprintf(copy, sizeof(copy), "%s", path);
    for (i = 3; copy[i]; ++i) {
        if (copy[i] == '\\' || copy[i] == '/') {
            char saved = copy[i];
            copy[i] = '\0';
            if (!CreateDirectoryA(copy, NULL)
                && GetLastError() != ERROR_ALREADY_EXISTS) return false;
            copy[i] = saved;
        }
    }
    return true;
}

static bool write_binary(const char *path, const uint8_t *data, size_t size)
{
    FILE *file;
    if (!mkdir_parents(path)) return false;
    file = fopen(path, "wb");
    if (!file) return false;
    if (size && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static void changes_free(CountryChanges *changes)
{
    size_t i;
    for (i = 0; i < changes->count; ++i) {
        DeleteFileA(changes->items[i].temporary);
        free(changes->items[i].new_data);
        free(changes->items[i].old_data);
    }
    memset(changes, 0, sizeof(*changes));
}

static bool changes_add(CountryChanges *changes, const char *path,
                        uint8_t *data, size_t size,
                        char *error, size_t error_size)
{
    CountryChange *change;
    if (changes->count >= sizeof(changes->items) / sizeof(changes->items[0])) {
        snprintf(error, error_size, "Trop de fichiers à modifier.");
        return false;
    }
    change = &changes->items[changes->count++];
    memset(change, 0, sizeof(*change));
    snprintf(change->path, sizeof(change->path), "%s", path);
    snprintf(change->temporary, sizeof(change->temporary),
             "%s.crispy-country.tmp", path);
    change->existed = path_is_file(path);
    if (change->existed) {
        change->old_data = read_binary(path, &change->old_size);
        if (!change->old_data) {
            snprintf(error, error_size, "Impossible de sauvegarder %s.", path);
            changes->count--;
            return false;
        }
    }
    change->new_data = data;
    change->new_size = size;
    return true;
}

static bool changes_commit(CountryChanges *changes,
                           char *error, size_t error_size)
{
    size_t i;
    for (i = 0; i < changes->count; ++i) {
        CountryChange *change = &changes->items[i];
        if (!write_binary(change->temporary,
                          change->new_data, change->new_size)) {
            snprintf(error, error_size,
                     "Impossible de préparer %s.", change->path);
            return false;
        }
    }
    for (i = 0; i < changes->count; ++i) {
        CountryChange *change = &changes->items[i];
        if (!MoveFileExA(change->temporary, change->path,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            size_t q;
            for (q = 0; q < i; ++q) {
                CountryChange *done = &changes->items[q];
                if (done->existed)
                    write_binary(done->path, done->old_data, done->old_size);
                else
                    DeleteFileA(done->path);
            }
            snprintf(error, error_size,
                     "Écriture annulée : impossible de remplacer %s.",
                     change->path);
            return false;
        }
        change->committed = true;
    }
    return true;
}

void country_create_request_defaults(CountryCreateRequest *request)
{
    memset(request, 0, sizeof(*request));
    request->culture = COUNTRY_CULTURE_WESTERN_EUROPEAN;
    request->color[0] = 80;
    request->color[1] = 110;
    request->color[2] = 150;
    request->color_ui[0] = 53;
    request->color_ui[1] = 73;
    request->color_ui[2] = 100;
    request->ruling_party = COUNTRY_IDEOLOGY_NEUTRALITY;
}

const char *country_culture_label(CountryCulture culture)
{
    return culture >= 0 && culture < COUNTRY_CULTURE_COUNT
        ? culture_labels[culture] : "";
}

const char *country_culture_file(CountryCulture culture)
{
    return culture >= 0 && culture < COUNTRY_CULTURE_COUNT
        ? culture_files[culture] : "";
}

const char *country_ideology_name(CountryIdeology ideology)
{
    return ideology >= 0 && ideology < COUNTRY_IDEOLOGY_COUNT
        ? ideologies[ideology] : "";
}

static bool valid_tag(const char *tag)
{
    return tag && strlen(tag) == 3
        && tag[0] >= 'A' && tag[0] <= 'Z'
        && tag[1] >= 'A' && tag[1] <= 'Z'
        && tag[2] >= 'A' && tag[2] <= 'Z';
}

static bool tag_exists_in_colors(const char *path, const char *tag)
{
    size_t size;
    uint8_t *raw = read_binary(path, &size);
    const char *cursor;
    bool found = false;
    if (!raw) return false;
    cursor = (const char *)raw;
    while (*cursor) {
        const char *line = cursor;
        const char *end;
        const char *p;
        while (*cursor && *cursor != '\n') cursor++;
        end = cursor;
        if (*cursor) cursor++;
        p = line;
        while (p < end && isspace((unsigned char)*p)) p++;
        if (end - p >= 3 && _strnicmp(p, tag, 3) == 0) {
            p += 3;
            while (p < end && isspace((unsigned char)*p)) p++;
            if (p < end && *p == '=') {
                found = true;
                break;
            }
        }
    }
    free(raw);
    return found;
}

bool country_creator_validate(const CountryCreateRequest *request,
                              const char *game_root, const char *mod_root,
                              char *error, size_t error_size)
{
    char culture_mod[CP_PATH_MAX] = "";
    char culture_game[CP_PATH_MAX] = "";
    char mod_colors[CP_PATH_MAX] = "";
    char game_colors[CP_PATH_MAX] = "";
    int i;
    if (error && error_size) error[0] = '\0';
#define FAIL(...) do { snprintf(error, error_size, __VA_ARGS__); return false; } while (0)
    if (!request || !mod_root || !cp_path_is_dir(mod_root))
        FAIL("Sélectionne un dossier de mod valide.");
    if (!valid_tag(request->tag))
        FAIL("Le tag doit contenir exactement trois lettres A-Z.");
    if (!request->name[0]) FAIL("Le nom du pays est obligatoire.");
    if (!request->adjective[0]) FAIL("L’adjectif est obligatoire.");
    if (!request->definite_name[0])
        FAIL("Le nom avec article est obligatoire.");
    if (request->culture < 0 || request->culture >= COUNTRY_CULTURE_COUNT)
        FAIL("Culture graphique invalide.");
    if (request->ruling_party < 0
        || request->ruling_party >= COUNTRY_IDEOLOGY_COUNT)
        FAIL("Parti dirigeant invalide.");
    if (request->capital < 0) FAIL("L’ID de la capitale doit être positif.");
    for (i = 0; i < 3; ++i) {
        if (request->color[i] < 0 || request->color[i] > 255
            || request->color_ui[i] < 0 || request->color_ui[i] > 255)
            FAIL("Chaque composante de couleur doit être entre 0 et 255.");
    }
    {
        char directory[CP_PATH_MAX];
        cp_path_join(directory, sizeof(directory), mod_root,
                     "common\\countries");
        cp_path_join(culture_mod, sizeof(culture_mod), directory,
                     country_culture_file(request->culture));
    }
    if (game_root && game_root[0]) {
        char directory[CP_PATH_MAX];
        cp_path_join(directory, sizeof(directory), game_root,
                     "common\\countries");
        cp_path_join(culture_game, sizeof(culture_game), directory,
                     country_culture_file(request->culture));
    }
    if (!path_is_file(culture_mod) && !path_is_file(culture_game))
        FAIL("Le fichier de culture %s est introuvable dans le mod et le jeu.",
             country_culture_file(request->culture));
    cp_path_join(mod_colors, sizeof(mod_colors), mod_root,
                 "common\\countries\\colors.txt");
    if (game_root && game_root[0])
        cp_path_join(game_colors, sizeof(game_colors), game_root,
                     "common\\countries\\colors.txt");
    if (path_is_file(mod_colors)) {
        if (tag_exists_in_colors(mod_colors, request->tag))
            FAIL("Le tag %s existe déjà dans le colors.txt du mod.",
                 request->tag);
    } else if (path_is_file(game_colors)
               && tag_exists_in_colors(game_colors, request->tag)) {
        FAIL("Le tag %s existe déjà dans le colors.txt vanilla.",
             request->tag);
    }
    return true;
#undef FAIL
}

static char *append_text_payload(const char *path, const char *addition,
                                 bool localisation)
{
    size_t old_size = 0;
    uint8_t *old = read_binary(path, &old_size);
    size_t bom = old_size >= 3 && old[0] == 0xEF
        && old[1] == 0xBB && old[2] == 0xBF ? 3 : 0;
    const char *body = old ? (const char *)old + bom : "";
    size_t body_size = old ? old_size - bom : 0;
    const char *header = localisation && !old ? "l_english:\n" : "";
    bool newline = body_size && body[body_size - 1] != '\n';
    size_t total = (localisation ? 3 : 0) + strlen(header)
        + body_size + (newline ? 1 : 0) + strlen(addition);
    char *output = malloc(total + 1);
    size_t at = 0;
    if (!output) {
        free(old);
        return NULL;
    }
    if (localisation) {
        output[at++] = (char)0xEF;
        output[at++] = (char)0xBB;
        output[at++] = (char)0xBF;
    }
    memcpy(output + at, header, strlen(header)); at += strlen(header);
    memcpy(output + at, body, body_size); at += body_size;
    if (newline) output[at++] = '\n';
    memcpy(output + at, addition, strlen(addition)); at += strlen(addition);
    output[at] = '\0';
    free(old);
    return output;
}

static bool buffer_add_loc_value(Buffer *buffer, const char *value)
{
    const unsigned char *p = (const unsigned char *)value;
    while (*p) {
        if (*p == '\\' || *p == '"') {
            if (!buffer_add_n(buffer, "\\", 1)) return false;
        }
        if (!buffer_add_n(buffer, (const char *)p, 1)) return false;
        p++;
    }
    return true;
}

static char *build_localisation(const CountryCreateRequest *request)
{
    Buffer out = {0};
    int i;
    if (!buffer_add(&out, "\n")) goto failure;
    for (i = 0; i < COUNTRY_IDEOLOGY_COUNT; ++i) {
        if (!buffer_printf(&out, " %s_%s: \"", request->tag, ideologies[i])
            || !buffer_add_loc_value(&out, request->name)
            || !buffer_printf(&out, "\"\n %s_%s_DEF: \"",
                              request->tag, ideologies[i])
            || !buffer_add_loc_value(&out, request->definite_name)
            || !buffer_printf(&out, "\"\n %s_%s_ADJ: \"",
                              request->tag, ideologies[i])
            || !buffer_add_loc_value(&out, request->adjective)
            || !buffer_add(&out, "\"\n")) goto failure;
    }
    if (!buffer_printf(&out, " %s: \"", request->tag)
        || !buffer_add_loc_value(&out, request->name)
        || !buffer_printf(&out, "\"\n %s_DEF: \"", request->tag)
        || !buffer_add_loc_value(&out, request->definite_name)
        || !buffer_printf(&out, "\"\n %s_ADJ: \"", request->tag)
        || !buffer_add_loc_value(&out, request->adjective)
        || !buffer_add(&out, "\"\n")) goto failure;
    return out.data;
failure:
    free(out.data);
    return NULL;
}

static uint8_t *make_tga(int width, int height, const int color[3],
                         size_t *size)
{
    size_t pixels = (size_t)width * height;
    uint8_t *data = calloc(18 + pixels * 3, 1);
    int x, y;
    if (!data) return NULL;
    data[2] = 2;
    data[12] = (uint8_t)(width & 255);
    data[13] = (uint8_t)(width >> 8);
    data[14] = (uint8_t)(height & 255);
    data[15] = (uint8_t)(height >> 8);
    data[16] = 24;
    data[17] = 0x20;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            bool edge = x == 0 || y == 0 || x == width - 1 || y == height - 1;
            size_t at = 18 + ((size_t)y * width + x) * 3;
            data[at] = (uint8_t)(edge ? color[2] / 3 : color[2]);
            data[at + 1] = (uint8_t)(edge ? color[1] / 3 : color[1]);
            data[at + 2] = (uint8_t)(edge ? color[0] / 3 : color[0]);
        }
    }
    *size = 18 + pixels * 3;
    return data;
}

static void safe_filename(const char *source, char *target, size_t size)
{
    size_t at = 0;
    const unsigned char *p = (const unsigned char *)source;
    while (*p && at + 1 < size) {
        unsigned char c = *p++;
        target[at++] = strchr("<>:\"/\\|?*", c) ? '_' : (char)c;
    }
    while (at && (target[at - 1] == ' ' || target[at - 1] == '.')) at--;
    target[at] = '\0';
    if (!at) snprintf(target, size, "Generated");
}

bool country_creator_execute(const CountryCreateRequest *request,
                             const char *game_root, const char *mod_root,
                             CountryCreateResult *result)
{
    CountryChanges changes = {0};
    Buffer colors = {0}, history = {0}, tag_line = {0};
    char *tag_payload = NULL, *colors_payload = NULL;
    char *loc_addition = NULL, *loc_payload = NULL;
    char history_name[180], relative[256];
    uint8_t *history_payload = NULL;
    bool success = false;
    int i;
    static const int flag_sizes[3][2] = {{82, 52}, {41, 26}, {10, 7}};
    memset(result, 0, sizeof(*result));
    if (!country_creator_validate(request, game_root, mod_root,
                                  result->error, sizeof(result->error)))
        return false;
    if (!cp_path_join(result->tag_file, sizeof(result->tag_file),
                      mod_root, "common\\country_tags\\zz_crispy_pandas_countries.txt")
        || !cp_path_join(result->colors_file, sizeof(result->colors_file),
                         mod_root, "common\\countries\\colors.txt")
        || !cp_path_join(result->localisation_file,
                         sizeof(result->localisation_file), mod_root,
                         "localisation\\english\\countries_l_english.yml"))
        goto path_error;
    safe_filename(request->name, history_name, sizeof(history_name));
    snprintf(relative, sizeof(relative), "history\\countries\\%s - %s.txt",
             request->tag, history_name);
    if (!cp_path_join(result->history_file, sizeof(result->history_file),
                      mod_root, relative))
        goto path_error;
    if (path_is_file(result->history_file)) {
        snprintf(result->error, sizeof(result->error),
                 "Le fichier historique existe déjà : %.700s.",
                 result->history_file);
        goto cleanup;
    }
    if (!buffer_printf(&tag_line, "%s = \"countries/%s\"\n",
                       request->tag, country_culture_file(request->culture)))
        goto memory_error;
    tag_payload = append_text_payload(result->tag_file, tag_line.data, false);
    if (!tag_payload) goto memory_error;
    if (!buffer_printf(&colors,
        "\n%s = {\n\tcolor = rgb { %d %d %d }\n"
        "\tcolor_ui = rgb { %d %d %d }\n}\n",
        request->tag, request->color[0], request->color[1], request->color[2],
        request->color_ui[0], request->color_ui[1], request->color_ui[2]))
        goto memory_error;
    colors_payload = append_text_payload(result->colors_file,
                                         colors.data, false);
    if (!colors_payload) goto memory_error;
    if (!buffer_printf(&history,
        "capital = %d\n"
        "set_convoys = 0\n"
        "set_research_slots = 2\n"
        "set_stability = 0.5\n"
        "set_war_support = 0.5\n"
        "set_oob = \"standard_templates\"\n\n"
        "add_ideas = {\n}\n\n"
        "set_technology = {\n\tinfantry_weapons = 1\n}\n\n"
        "set_politics = {\n\truling_party = %s\n\telections_allowed = no\n}\n\n"
        "set_popularities = {\n"
        "\tneutrality = %d\n\tdemocratic = %d\n"
        "\tfascism = %d\n\tcommunism = %d\n}\n",
        request->capital, country_ideology_name(request->ruling_party),
        request->ruling_party == COUNTRY_IDEOLOGY_NEUTRALITY ? 100 : 0,
        request->ruling_party == COUNTRY_IDEOLOGY_DEMOCRATIC ? 100 : 0,
        request->ruling_party == COUNTRY_IDEOLOGY_FASCISM ? 100 : 0,
        request->ruling_party == COUNTRY_IDEOLOGY_COMMUNISM ? 100 : 0))
        goto memory_error;
    history_payload = (uint8_t *)history.data;
    history.data = NULL;
    loc_addition = build_localisation(request);
    if (!loc_addition) goto memory_error;
    loc_payload = append_text_payload(result->localisation_file,
                                      loc_addition, true);
    if (!loc_payload) goto memory_error;
    if (!changes_add(&changes, result->tag_file,
                     (uint8_t *)tag_payload, strlen(tag_payload),
                     result->error, sizeof(result->error))) goto cleanup;
    tag_payload = NULL;
    if (!changes_add(&changes, result->colors_file,
                     (uint8_t *)colors_payload, strlen(colors_payload),
                     result->error, sizeof(result->error))) goto cleanup;
    colors_payload = NULL;
    if (!changes_add(&changes, result->history_file,
                     history_payload, strlen((char *)history_payload),
                     result->error, sizeof(result->error))) goto cleanup;
    history_payload = NULL;
    if (!changes_add(&changes, result->localisation_file,
                     (uint8_t *)loc_payload, strlen(loc_payload + 3) + 3,
                     result->error, sizeof(result->error))) goto cleanup;
    loc_payload = NULL;
    if (request->create_placeholder_flags) {
        static const char *flag_relatives[3] = {
            "gfx\\flags", "gfx\\flags\\medium", "gfx\\flags\\small"
        };
        for (i = 0; i < 3; ++i) {
            char directory[CP_PATH_MAX], filename[16];
            uint8_t *flag;
            size_t flag_size;
            if (!cp_path_join(directory, sizeof(directory),
                              mod_root, flag_relatives[i])) goto path_error;
            snprintf(filename, sizeof(filename), "%s.tga", request->tag);
            if (!cp_path_join(result->flag_files[i],
                              sizeof(result->flag_files[i]),
                              directory, filename)) goto path_error;
            if (path_is_file(result->flag_files[i])) {
                snprintf(result->error, sizeof(result->error),
                         "Le drapeau existe déjà : %.700s.",
                         result->flag_files[i]);
                goto cleanup;
            }
            flag = make_tga(flag_sizes[i][0], flag_sizes[i][1],
                            request->color, &flag_size);
            if (!flag) goto memory_error;
            if (!changes_add(&changes, result->flag_files[i],
                             flag, flag_size,
                             result->error, sizeof(result->error))) {
                free(flag);
                goto cleanup;
            }
        }
    }
    if (!changes_commit(&changes, result->error, sizeof(result->error)))
        goto cleanup;
    result->changed_files = changes.count;
    success = true;
    goto cleanup;
path_error:
    snprintf(result->error, sizeof(result->error), "Chemin de sortie trop long.");
    goto cleanup;
memory_error:
    snprintf(result->error, sizeof(result->error), "Mémoire insuffisante.");
cleanup:
    free(tag_line.data);
    free(colors.data);
    free(history.data);
    free(tag_payload);
    free(colors_payload);
    free(history_payload);
    free(loc_addition);
    free(loc_payload);
    changes_free(&changes);
    return success;
}
