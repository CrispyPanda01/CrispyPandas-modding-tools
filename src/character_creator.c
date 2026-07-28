#include "character_creator.h"

#include <windows.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portrait_image.h"
#include "province_transfer.h"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} CharBuffer;

typedef struct {
    char target[CP_PATH_MAX];
    uint8_t *data;
    size_t size;
    char temp[CP_PATH_MAX];
    char backup[CP_PATH_MAX];
    bool had_original;
    bool committed;
} CharacterFileChange;

typedef struct {
    CharacterFileChange *items;
    size_t count;
    size_t capacity;
} CharacterChangeList;

static bool buffer_add_n(CharBuffer *buffer, const char *text, size_t length)
{
    if (buffer->length + length + 1 > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 4096;
        char *grown;
        while (capacity < buffer->length + length + 1) capacity *= 2;
        grown = realloc(buffer->data, capacity);
        if (!grown) return false;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool buffer_add(CharBuffer *buffer, const char *text)
{
    return buffer_add_n(buffer, text, strlen(text));
}

static bool buffer_addf(CharBuffer *buffer, const char *format, ...)
{
    va_list arguments;
    va_list copy;
    int length;
    char *target;
    va_start(arguments, format);
    va_copy(copy, arguments);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(arguments);
        return false;
    }
    target = malloc((size_t)length + 1);
    if (!target) {
        va_end(arguments);
        return false;
    }
    vsnprintf(target, (size_t)length + 1, format, arguments);
    va_end(arguments);
    if (!buffer_add_n(buffer, target, (size_t)length)) {
        free(target);
        return false;
    }
    free(target);
    return true;
}

static char *read_text_file(const char *path, char *error, size_t error_size)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *text;
    if (!file) {
        snprintf(error, error_size, "Impossible de lire %.600s", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    rewind(file);
    text = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (!text || fread(text, 1, (size_t)length, file) != (size_t)length) {
        free(text);
        fclose(file);
        snprintf(error, error_size, "Lecture incomplète de %.580s", path);
        return NULL;
    }
    fclose(file);
    text[length] = '\0';
    return text;
}

static bool make_directory(const char *path, char *error, size_t error_size)
{
    if (CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
        return true;
    snprintf(error, error_size, "Impossible de créer %.600s", path);
    return false;
}

static bool make_child_directory(const char *parent, const char *child,
                                 char *output, size_t output_size,
                                 char *error, size_t error_size)
{
    if (!cp_path_join(output, output_size, parent, child)
        || !make_directory(output, error, error_size)) {
        if (!error[0]) snprintf(error, error_size, "Chemin de dossier trop long.");
        return false;
    }
    return true;
}

static bool changes_add(CharacterChangeList *changes, const char *target,
                        uint8_t *data, size_t size,
                        char *error, size_t error_size)
{
    size_t i;
    for (i = 0; i < changes->count; ++i) {
        if (_stricmp(changes->items[i].target, target) == 0) {
            snprintf(error, error_size,
                     "Deux sorties ciblent le même fichier %.550s", target);
            return false;
        }
    }
    if (changes->count == changes->capacity) {
        size_t capacity = changes->capacity ? changes->capacity * 2 : 8;
        CharacterFileChange *grown =
            realloc(changes->items, capacity * sizeof(*grown));
        if (!grown) return false;
        changes->items = grown;
        changes->capacity = capacity;
    }
    memset(&changes->items[changes->count], 0,
           sizeof(changes->items[changes->count]));
    snprintf(changes->items[changes->count].target, CP_PATH_MAX, "%s", target);
    changes->items[changes->count].data = data;
    changes->items[changes->count].size = size;
    changes->count++;
    return true;
}

static void changes_free(CharacterChangeList *changes)
{
    size_t i;
    for (i = 0; i < changes->count; ++i) free(changes->items[i].data);
    free(changes->items);
    memset(changes, 0, sizeof(*changes));
}

static bool write_bytes(const char *path, const uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    if (size && fwrite(data, 1, size, file) != size) {
        fclose(file);
        DeleteFileA(path);
        return false;
    }
    if (fclose(file) != 0) {
        DeleteFileA(path);
        return false;
    }
    return true;
}

static bool changes_commit(CharacterChangeList *changes,
                           char *error, size_t error_size)
{
    DWORD pid = GetCurrentProcessId();
    ULONGLONG tick = GetTickCount64();
    size_t i, j;
    for (i = 0; i < changes->count; ++i) {
        CharacterFileChange *change = &changes->items[i];
        snprintf(change->temp, sizeof(change->temp), "%.4000s.crispy.%lu.%llu.tmp",
                 change->target, (unsigned long)pid, (unsigned long long)tick);
        snprintf(change->backup, sizeof(change->backup), "%.4000s.crispy.%lu.%llu.bak",
                 change->target, (unsigned long)pid, (unsigned long long)tick);
        DeleteFileA(change->temp);
        DeleteFileA(change->backup);
        if (!write_bytes(change->temp, change->data, change->size)) {
            snprintf(error, error_size, "Impossible de préparer %.600s", change->target);
            for (j = 0; j <= i; ++j) DeleteFileA(changes->items[j].temp);
            return false;
        }
    }
    for (i = 0; i < changes->count; ++i) {
        CharacterFileChange *change = &changes->items[i];
        change->had_original = cp_path_exists(change->target);
        if (change->had_original
            && !MoveFileExA(change->target, change->backup, MOVEFILE_WRITE_THROUGH)) {
            snprintf(error, error_size, "Impossible de sauvegarder %.600s", change->target);
            goto rollback;
        }
        if (!MoveFileExA(change->temp, change->target, MOVEFILE_WRITE_THROUGH)) {
            if (change->had_original)
                MoveFileExA(change->backup, change->target, MOVEFILE_WRITE_THROUGH);
            snprintf(error, error_size, "Impossible de remplacer %.600s", change->target);
            goto rollback;
        }
        change->committed = true;
    }
    for (i = 0; i < changes->count; ++i)
        if (changes->items[i].had_original) DeleteFileA(changes->items[i].backup);
    return true;

rollback:
    for (j = 0; j < changes->count; ++j) {
        CharacterFileChange *change = &changes->items[j];
        DeleteFileA(change->temp);
        if (change->committed) {
            DeleteFileA(change->target);
            if (change->had_original)
                MoveFileExA(change->backup, change->target, MOVEFILE_WRITE_THROUGH);
        }
    }
    return false;
}

static bool safe_identifier(const char *value)
{
    size_t i;
    if (!value || !value[0]) return false;
    for (i = 0; value[i]; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!isalnum(c) && c != '_' && c != '-') return false;
    }
    return true;
}

static bool valid_tag(const char *tag)
{
    return tag && strlen(tag) == 3
        && isalnum((unsigned char)tag[0])
        && isalnum((unsigned char)tag[1])
        && isalnum((unsigned char)tag[2]);
}

void character_create_request_defaults(CharacterCreateRequest *request)
{
    memset(request, 0, sizeof(*request));
    snprintf(request->ideology, sizeof(request->ideology), "despotism");
    snprintf(request->expire, sizeof(request->expire), "1965.1.1.1");
    snprintf(request->advisor_slot, sizeof(request->advisor_slot),
             "political_advisor");
    request->advisor_cost = 150;
    request->land_skill = 1;
    request->attack_skill = 1;
    request->defense_skill = 1;
    request->planning_skill = 1;
    request->logistics_skill = 1;
    request->navy_skill = 1;
    request->navy_attack_skill = 1;
    request->navy_defense_skill = 1;
    request->maneuvering_skill = 1;
    request->coordination_skill = 1;
    snprintf(request->scientist_specialization,
             sizeof(request->scientist_specialization),
             "specialization_nuclear");
    request->scientist_skill = 1;
}

bool character_create_validate(const CharacterCreateRequest *request,
                               char *error, size_t error_size)
{
    const char *prefix;
    if (!request || !valid_tag(request->country_tag)) {
        snprintf(error, error_size, "Le tag doit contenir exactement 3 caractères.");
        return false;
    }
    if (!safe_identifier(request->token)) {
        snprintf(error, error_size,
                 "Le token doit utiliser seulement lettres, chiffres, _ ou -.");
        return false;
    }
    prefix = request->token;
    if (_strnicmp(prefix, request->country_tag, 3) != 0 || prefix[3] != '_') {
        snprintf(error, error_size, "Le token doit commencer par %s_.",
                 request->country_tag);
        return false;
    }
    if (!request->name[0]) {
        snprintf(error, error_size, "Le nom du personnage est obligatoire.");
        return false;
    }
    if (strchr(request->name, '\r') || strchr(request->name, '\n')) {
        snprintf(error, error_size, "Le nom ne peut pas contenir de retour de ligne.");
        return false;
    }
    if (!request->roles) {
        snprintf(error, error_size, "Sélectionnez au moins un rôle.");
        return false;
    }
    if (!request->large_portrait[0] || !cp_path_exists(request->large_portrait)) {
        snprintf(error, error_size, "Sélectionnez un portrait large valide.");
        return false;
    }
    if (request->small_portrait[0] && !cp_path_exists(request->small_portrait)) {
        snprintf(error, error_size, "Le portrait small est introuvable.");
        return false;
    }
    if ((request->roles & CHARACTER_ROLE_COUNTRY_LEADER)
        && !safe_identifier(request->ideology)) {
        snprintf(error, error_size, "L'idéologie est invalide.");
        return false;
    }
    if ((request->roles & CHARACTER_ROLE_ADVISOR)
        && (!safe_identifier(request->advisor_slot)
            || request->advisor_cost < 0 || request->advisor_cost > 10000)) {
        snprintf(error, error_size, "Les paramètres d'advisor sont invalides.");
        return false;
    }
    return true;
}

static bool add_escaped_name(CharBuffer *buffer, const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;
    if (!buffer_add(buffer, "\t\tname = \"")) return false;
    while (*cursor) {
        if ((*cursor == '"' || *cursor == '\\') && !buffer_add(buffer, "\\"))
            return false;
        if (!buffer_add_n(buffer, (const char *)cursor, 1)) return false;
        cursor++;
    }
    return buffer_add(buffer, "\"\r\n");
}

static bool add_traits(CharBuffer *buffer, const char *indent, const char *list)
{
    const char *cursor = list;
    bool opened = false;
    while (cursor && *cursor) {
        char token[128];
        size_t n = 0;
        while (*cursor && (isspace((unsigned char)*cursor)
               || *cursor == ',' || *cursor == ';')) cursor++;
        while (*cursor && !isspace((unsigned char)*cursor)
               && *cursor != ',' && *cursor != ';' && n + 1 < sizeof(token))
            token[n++] = *cursor++;
        token[n] = '\0';
        if (!n) continue;
        if (!safe_identifier(token)) return false;
        if (!opened) {
            if (!buffer_addf(buffer, "%straits = {", indent)) return false;
            opened = true;
        }
        if (!buffer_addf(buffer, " %s", token)) return false;
    }
    return !opened || buffer_add(buffer, " }\r\n");
}

static bool build_character_block(const CharacterCreateRequest *request,
                                  char **output,
                                  char *error, size_t error_size)
{
    CharBuffer buffer = {0};
    uint32_t civilian_roles = CHARACTER_ROLE_COUNTRY_LEADER
        | CHARACTER_ROLE_ADVISOR | CHARACTER_ROLE_SCIENTIST;
    const char *token = request->token;
    bool ok =
        buffer_addf(&buffer, "\r\n\t%s = {\r\n", token)
        && add_escaped_name(&buffer, request->name)
        && buffer_add(&buffer, "\t\tportraits = {\r\n");
    if (ok && (request->roles & civilian_roles))
        ok = buffer_addf(&buffer,
            "\t\t\tcivilian = {\r\n"
            "\t\t\t\tlarge = GFX_portrait_%s\r\n"
            "\t\t\t\tsmall = GFX_portrait_%s_small\r\n"
            "\t\t\t}\r\n", token, token);
    if (ok && (request->roles
        & (CHARACTER_ROLE_GENERAL | CHARACTER_ROLE_FIELD_MARSHAL)))
        ok = buffer_addf(&buffer,
            "\t\t\tarmy = {\r\n"
            "\t\t\t\tlarge = GFX_portrait_%s\r\n"
            "\t\t\t\tsmall = GFX_portrait_%s_small\r\n"
            "\t\t\t}\r\n", token, token);
    if (ok && (request->roles & CHARACTER_ROLE_NAVY_LEADER))
        ok = buffer_addf(&buffer,
            "\t\t\tnavy = {\r\n"
            "\t\t\t\tlarge = GFX_portrait_%s\r\n"
            "\t\t\t\tsmall = GFX_portrait_%s_small\r\n"
            "\t\t\t}\r\n", token, token);
    ok = ok && buffer_add(&buffer, "\t\t}\r\n");

    if (ok && (request->roles & CHARACTER_ROLE_COUNTRY_LEADER)) {
        ok = buffer_addf(&buffer,
            "\t\tcountry_leader = {\r\n"
            "\t\t\tideology = %s\r\n"
            "\t\t\texpire = \"%s\"\r\n"
            "\t\t\tid = -1\r\n",
            request->ideology, request->expire)
            && add_traits(&buffer, "\t\t\t", request->country_traits)
            && buffer_add(&buffer, "\t\t}\r\n");
    }
    if (ok && (request->roles & CHARACTER_ROLE_ADVISOR)) {
        ok = buffer_addf(&buffer,
            "\t\tadvisor = {\r\n"
            "\t\t\tslot = %s\r\n"
            "\t\t\tidea_token = %s\r\n"
            "\t\t\tcost = %d\r\n",
            request->advisor_slot, token, request->advisor_cost);
        if (ok && request->advisor_ledger[0])
            ok = buffer_addf(&buffer, "\t\t\tledger = %s\r\n",
                             request->advisor_ledger);
        ok = ok && buffer_addf(&buffer,
            "\t\t\tallowed = { original_tag = %s }\r\n"
            "\t\t\tai_will_do = { factor = 1 }\r\n",
            request->country_tag)
            && add_traits(&buffer, "\t\t\t", request->advisor_traits)
            && buffer_add(&buffer, "\t\t}\r\n");
    }
    if (ok && (request->roles & CHARACTER_ROLE_GENERAL)) {
        ok = buffer_addf(&buffer,
            "\t\tcorps_commander = {\r\n"
            "\t\t\tskill = %d\r\n"
            "\t\t\tattack_skill = %d\r\n"
            "\t\t\tdefense_skill = %d\r\n"
            "\t\t\tplanning_skill = %d\r\n"
            "\t\t\tlogistics_skill = %d\r\n"
            "\t\t\tlegacy_id = -1\r\n",
            request->land_skill, request->attack_skill,
            request->defense_skill, request->planning_skill,
            request->logistics_skill)
            && add_traits(&buffer, "\t\t\t", request->land_traits)
            && buffer_add(&buffer, "\t\t}\r\n");
    }
    if (ok && (request->roles & CHARACTER_ROLE_FIELD_MARSHAL)) {
        ok = buffer_addf(&buffer,
            "\t\tfield_marshal = {\r\n"
            "\t\t\tskill = %d\r\n"
            "\t\t\tattack_skill = %d\r\n"
            "\t\t\tdefense_skill = %d\r\n"
            "\t\t\tplanning_skill = %d\r\n"
            "\t\t\tlogistics_skill = %d\r\n"
            "\t\t\tlegacy_id = -1\r\n",
            request->land_skill, request->attack_skill,
            request->defense_skill, request->planning_skill,
            request->logistics_skill)
            && add_traits(&buffer, "\t\t\t", request->land_traits)
            && buffer_add(&buffer, "\t\t}\r\n");
    }
    if (ok && (request->roles & CHARACTER_ROLE_NAVY_LEADER)) {
        ok = buffer_addf(&buffer,
            "\t\tnavy_leader = {\r\n"
            "\t\t\tskill = %d\r\n"
            "\t\t\tattack_skill = %d\r\n"
            "\t\t\tdefense_skill = %d\r\n"
            "\t\t\tmaneuvering_skill = %d\r\n"
            "\t\t\tcoordination_skill = %d\r\n"
            "\t\t\tlegacy_id = -1\r\n",
            request->navy_skill, request->navy_attack_skill,
            request->navy_defense_skill, request->maneuvering_skill,
            request->coordination_skill)
            && add_traits(&buffer, "\t\t\t", request->navy_traits)
            && buffer_add(&buffer, "\t\t}\r\n");
    }
    if (ok && (request->roles & CHARACTER_ROLE_SCIENTIST)) {
        ok = buffer_add(&buffer, "\t\tscientist = {\r\n")
            && add_traits(&buffer, "\t\t\t", request->scientist_traits);
        if (ok && request->scientist_specialization[0])
            ok = buffer_addf(&buffer,
                "\t\t\tskills = { %s = %d }\r\n",
                request->scientist_specialization,
                request->scientist_skill);
        ok = ok && buffer_add(&buffer, "\t\t}\r\n");
    }
    ok = ok && buffer_add(&buffer, "\t}\r\n");
    if (!ok) {
        free(buffer.data);
        snprintf(error, error_size,
                 "Mémoire insuffisante ou token de trait invalide.");
        return false;
    }
    *output = buffer.data;
    return true;
}

static size_t final_root_close(const char *text)
{
    size_t i;
    size_t last = (size_t)-1;
    bool quoted = false;
    for (i = 0; text[i]; ++i) {
        if (!quoted && text[i] == '#') {
            while (text[i] && text[i] != '\n') i++;
            if (!text[i]) break;
        } else if (text[i] == '"' && (i == 0 || text[i - 1] != '\\')) {
            quoted = !quoted;
        } else if (!quoted && text[i] == '}') {
            last = i;
        }
    }
    return last;
}

static char *insert_before_final_close(const char *existing, const char *addition,
                                       char *error, size_t error_size)
{
    size_t close = final_root_close(existing);
    CharBuffer output = {0};
    if (close == (size_t)-1
        || !buffer_add_n(&output, existing, close)
        || !buffer_add(&output, addition)
        || !buffer_add(&output, existing + close)) {
        free(output.data);
        snprintf(error, error_size, "Bloc racine du fichier généré invalide.");
        return NULL;
    }
    return output.data;
}

static bool text_has_assignment(const char *text, const char *token)
{
    const char *cursor = text;
    size_t length = strlen(token);
    while ((cursor = strstr(cursor, token)) != NULL) {
        const char *after = cursor + length;
        while (*after && isspace((unsigned char)*after)) after++;
        if ((cursor == text
             || (!isalnum((unsigned char)cursor[-1])
                 && cursor[-1] != '_' && cursor[-1] != '-'))
            && *after == '=') return true;
        cursor += length;
    }
    return false;
}

typedef struct {
    const char *needle;
    bool found;
} SearchContext;

static bool search_file_visitor(const char *path, const char *name, void *user)
{
    SearchContext *context = user;
    char error[8] = "";
    char *text;
    (void)name;
    if (context->found) return false;
    text = read_text_file(path, error, sizeof(error));
    if (text) {
        context->found = text_has_assignment(text, context->needle);
        free(text);
    }
    return !context->found;
}

static bool token_exists_in_root(const char *root, const char *relative,
                                 const char *token)
{
    char directory[CP_PATH_MAX];
    SearchContext context = {token, false};
    if (!cp_path_join(directory, sizeof(directory), root, relative)
        || !cp_path_is_dir(directory)) return false;
    cp_visit_txt_files(directory, search_file_visitor, &context);
    return context.found;
}

typedef struct {
    char tag[8];
    char path[CP_PATH_MAX];
    bool found;
} HistorySearch;

static bool history_file_visitor(const char *path, const char *name, void *user)
{
    HistorySearch *search = user;
    size_t length = strlen(search->tag);
    if (_strnicmp(name, search->tag, length) == 0
        && (name[length] == ' ' || name[length] == '-')) {
        snprintf(search->path, sizeof(search->path), "%s", path);
        search->found = true;
        return false;
    }
    return true;
}

static bool find_history_file(const char *root, const char *tag,
                              char *path, size_t path_size)
{
    char directory[CP_PATH_MAX];
    HistorySearch search = {{0}, {0}, false};
    snprintf(search.tag, sizeof(search.tag), "%s", tag);
    if (!cp_path_join(directory, sizeof(directory), root, "history\\countries")
        || !cp_path_is_dir(directory)) return false;
    cp_visit_txt_files(directory, history_file_visitor, &search);
    if (!search.found) return false;
    snprintf(path, path_size, "%s", search.path);
    return true;
}

static const char *filename_of(const char *path)
{
    const char *a = strrchr(path, '\\');
    const char *b = strrchr(path, '/');
    const char *best = a > b ? a : b;
    return best ? best + 1 : path;
}

static char *build_container_file(const char *path, const char *root,
                                  const char *addition,
                                  char *error, size_t error_size)
{
    char *existing;
    char *output;
    if (!cp_path_exists(path)) {
        CharBuffer created = {0};
        if (!buffer_addf(&created, "%s = {\r\n", root)
            || !buffer_add(&created, addition)
            || !buffer_add(&created, "}\r\n")) {
            free(created.data);
            return NULL;
        }
        return created.data;
    }
    existing = read_text_file(path, error, error_size);
    if (!existing) return NULL;
    if (!province_transfer_validate_syntax(existing, error, error_size)) {
        free(existing);
        return NULL;
    }
    output = insert_before_final_close(existing, addition, error, error_size);
    free(existing);
    return output;
}

static bool contains_recruit(const char *text, const char *token)
{
    const char *cursor = text;
    while ((cursor = strstr(cursor, "recruit_character")) != NULL) {
        const char *equals = strchr(cursor, '=');
        if (!equals) break;
        equals++;
        while (*equals && isspace((unsigned char)*equals)) equals++;
        if (strncmp(equals, token, strlen(token)) == 0
            && !isalnum((unsigned char)equals[strlen(token)])
            && equals[strlen(token)] != '_'
            && equals[strlen(token)] != '-') return true;
        cursor = equals;
    }
    return false;
}

static char *build_history_output(const char *source, const char *token,
                                  char *error, size_t error_size)
{
    char *existing = source && source[0]
        ? read_text_file(source, error, error_size) : NULL;
    CharBuffer output = {0};
    if (source && source[0] && !existing) return NULL;
    if (existing && !province_transfer_validate_syntax(existing, error, error_size)) {
        free(existing);
        return NULL;
    }
    if (existing && contains_recruit(existing, token)) {
        free(existing);
        snprintf(error, error_size, "Le character %s est déjà recruté.", token);
        return NULL;
    }
    if (existing && !buffer_add(&output, existing)) goto memory_error;
    if (output.length && output.data[output.length - 1] != '\n'
        && !buffer_add(&output, "\r\n")) goto memory_error;
    if (!buffer_addf(&output, "\r\n# Ajouté par Crispy Pandas\r\n"
                     "recruit_character = %s\r\n", token)) goto memory_error;
    free(existing);
    return output.data;
memory_error:
    free(existing);
    free(output.data);
    snprintf(error, error_size, "Mémoire insuffisante pour l'historique du pays.");
    return NULL;
}

static bool make_portrait_payload(const char *source_path, int width, int height,
                                  uint8_t **payload, size_t *payload_size,
                                  char *error, size_t error_size)
{
    PortraitImage source = {0}, resized = {0};
    bool success = portrait_image_load(source_path, &source, error, error_size)
        && portrait_image_resize_cover(&source, width, height, &resized,
                                       error, error_size)
        && portrait_image_make_dds(&resized, payload, payload_size,
                                   error, error_size);
    portrait_image_free(&source);
    portrait_image_free(&resized);
    return success;
}

static bool make_advisor_small_payload(const char *source_path,
                                       uint8_t **payload, size_t *payload_size,
                                       char *error, size_t error_size)
{
    PortraitImage source = {0}, composed = {0};
    bool success = portrait_image_load(source_path, &source, error, error_size)
        && portrait_image_compose_advisor_small(&source, &composed,
                                               error, error_size)
        && portrait_image_make_dds(&composed, payload, payload_size,
                                   error, error_size);
    portrait_image_free(&source);
    portrait_image_free(&composed);
    return success;
}

bool character_create_execute(const char *game_root, const char *mod_root,
                              const CharacterCreateRequest *request,
                              CharacterCreateResult *result)
{
    CharacterChangeList changes = {0};
    char common_dir[CP_PATH_MAX], characters_dir[CP_PATH_MAX];
    char interface_dir[CP_PATH_MAX];
    char gfx_dir[CP_PATH_MAX], leaders_dir[CP_PATH_MAX], tag_leaders_dir[CP_PATH_MAX];
    char gfx_interface_dir[CP_PATH_MAX], ideas_dir[CP_PATH_MAX];
    char history_dir[CP_PATH_MAX], countries_dir[CP_PATH_MAX];
    char filename[256];
    char source_history[CP_PATH_MAX] = "";
    char *block = NULL, *character_text = NULL, *gfx_text = NULL, *history_text = NULL;
    uint8_t *large_dds = NULL, *small_dds = NULL;
    size_t large_size = 0, small_size = 0;
    CharBuffer gfx_addition = {0};
    bool success = false;

    memset(result, 0, sizeof(*result));
    if (!game_root || !mod_root || !cp_path_is_dir(mod_root)
        || !character_create_validate(request, result->error,
                                      sizeof(result->error))) return false;
    if (token_exists_in_root(game_root, "common\\characters", request->token)
        || token_exists_in_root(mod_root, "common\\characters", request->token)) {
        snprintf(result->error, sizeof(result->error),
                 "Le token %s existe déjà dans le jeu ou le mod.", request->token);
        return false;
    }

    if (!make_child_directory(mod_root, "common", common_dir, sizeof(common_dir),
                              result->error, sizeof(result->error))
        || !make_child_directory(common_dir, "characters", characters_dir,
                                 sizeof(characters_dir),
                                 result->error, sizeof(result->error))
        || !make_child_directory(mod_root, "interface", interface_dir,
                                 sizeof(interface_dir),
                                 result->error, sizeof(result->error))
        || !make_child_directory(mod_root, "gfx", gfx_dir, sizeof(gfx_dir),
                                 result->error, sizeof(result->error))
        || !make_child_directory(gfx_dir, "leaders", leaders_dir,
                                 sizeof(leaders_dir),
                                 result->error, sizeof(result->error))
        || !make_child_directory(leaders_dir, request->country_tag, tag_leaders_dir,
                                 sizeof(tag_leaders_dir),
                                 result->error, sizeof(result->error))
        || !make_child_directory(gfx_dir, "interface", gfx_interface_dir,
                                 sizeof(gfx_interface_dir),
                                 result->error, sizeof(result->error))
        || !make_child_directory(gfx_interface_dir, "ideas", ideas_dir,
                                 sizeof(ideas_dir),
                                 result->error, sizeof(result->error))
        || !make_child_directory(mod_root, "history", history_dir,
                                 sizeof(history_dir),
                                 result->error, sizeof(result->error))
        || !make_child_directory(history_dir, "countries", countries_dir,
                                 sizeof(countries_dir),
                                 result->error, sizeof(result->error))) goto cleanup;

    snprintf(filename, sizeof(filename), "zz_crispy_pandas_%s.txt",
             request->country_tag);
    if (!cp_path_join(result->character_file, sizeof(result->character_file),
                      characters_dir, filename)
        || !cp_path_join(result->gfx_file, sizeof(result->gfx_file),
                         interface_dir, "crispy_pandas_characters.gfx")) {
        snprintf(result->error, sizeof(result->error), "Chemin de sortie trop long.");
        goto cleanup;
    }
    snprintf(filename, sizeof(filename), "portrait_%s.dds", request->token);
    if (!cp_path_join(result->large_portrait_file,
                      sizeof(result->large_portrait_file),
                      tag_leaders_dir, filename)) goto path_error;
    snprintf(filename, sizeof(filename), "idea_%s.dds", request->token);
    if (!cp_path_join(result->small_portrait_file,
                      sizeof(result->small_portrait_file),
                      ideas_dir, filename)) goto path_error;

    if (cp_path_exists(result->large_portrait_file)
        || cp_path_exists(result->small_portrait_file)) {
        snprintf(result->error, sizeof(result->error),
                 "Un portrait de sortie existe déjà pour %s.", request->token);
        goto cleanup;
    }
    if (!build_character_block(request, &block,
                               result->error, sizeof(result->error)))
        goto cleanup;
    character_text = build_container_file(result->character_file, "characters",
                                          block,
                                          result->error, sizeof(result->error));
    if (!character_text
        || !province_transfer_validate_syntax(character_text,
                                              result->error,
                                              sizeof(result->error)))
        goto cleanup;

    if (!buffer_addf(&gfx_addition,
        "\r\n\tspriteType = {\r\n"
        "\t\tname = \"GFX_portrait_%s\"\r\n"
        "\t\ttexturefile = \"gfx/leaders/%s/portrait_%s.dds\"\r\n"
        "\t}\r\n"
        "\tspriteType = {\r\n"
        "\t\tname = \"GFX_portrait_%s_small\"\r\n"
        "\t\ttexturefile = \"gfx/interface/ideas/idea_%s.dds\"\r\n"
        "\t}\r\n",
        request->token, request->country_tag, request->token,
        request->token, request->token)) {
        snprintf(result->error, sizeof(result->error), "Mémoire insuffisante pour le GFX.");
        goto cleanup;
    }
    gfx_text = build_container_file(result->gfx_file, "spriteTypes",
                                    gfx_addition.data,
                                    result->error, sizeof(result->error));
    if (!gfx_text
        || !province_transfer_validate_syntax(gfx_text,
                                              result->error,
                                              sizeof(result->error)))
        goto cleanup;

    if (!find_history_file(mod_root, request->country_tag,
                           source_history, sizeof(source_history)))
        find_history_file(game_root, request->country_tag,
                          source_history, sizeof(source_history));
    if (source_history[0]) {
        if (!cp_path_join(result->history_file, sizeof(result->history_file),
                          countries_dir, filename_of(source_history)))
            goto path_error;
    } else {
        snprintf(filename, sizeof(filename), "%s - Generated.txt",
                 request->country_tag);
        if (!cp_path_join(result->history_file, sizeof(result->history_file),
                          countries_dir, filename))
            goto path_error;
    }
    history_text = build_history_output(source_history, request->token,
                                        result->error, sizeof(result->error));
    if (!history_text
        || !province_transfer_validate_syntax(history_text,
                                              result->error,
                                              sizeof(result->error)))
        goto cleanup;

    if (!make_portrait_payload(request->large_portrait, 156, 210,
                               &large_dds, &large_size,
                               result->error, sizeof(result->error))
        || (request->small_portrait[0]
            ? !make_portrait_payload(request->small_portrait,
                                     65, 67, &small_dds, &small_size,
                                     result->error, sizeof(result->error))
            : !make_advisor_small_payload(request->large_portrait,
                                          &small_dds, &small_size,
                                          result->error, sizeof(result->error))))
        goto cleanup;

    if (!changes_add(&changes, result->character_file,
                     (uint8_t *)character_text, strlen(character_text),
                     result->error, sizeof(result->error)))
        goto cleanup;
    character_text = NULL;
    if (!changes_add(&changes, result->gfx_file,
                     (uint8_t *)gfx_text, strlen(gfx_text),
                     result->error, sizeof(result->error)))
        goto cleanup;
    gfx_text = NULL;
    if (!changes_add(&changes, result->history_file,
                     (uint8_t *)history_text, strlen(history_text),
                     result->error, sizeof(result->error)))
        goto cleanup;
    history_text = NULL;
    if (!changes_add(&changes, result->large_portrait_file,
                     large_dds, large_size,
                     result->error, sizeof(result->error)))
        goto cleanup;
    large_dds = NULL;
    if (!changes_add(&changes, result->small_portrait_file,
                     small_dds, small_size,
                     result->error, sizeof(result->error)))
        goto cleanup;
    small_dds = NULL;
    if (!changes_commit(&changes, result->error, sizeof(result->error)))
        goto cleanup;
    result->changed_files = changes.count;
    success = true;
    goto cleanup;

path_error:
    snprintf(result->error, sizeof(result->error), "Chemin de sortie trop long.");
cleanup:
    free(block);
    free(character_text);
    free(gfx_text);
    free(history_text);
    free(large_dds);
    free(small_dds);
    free(gfx_addition.data);
    changes_free(&changes);
    return success;
}

bool character_trait_list_contains(const char *list, const char *token)
{
    const char *cursor = list;
    size_t token_length = strlen(token);
    while (cursor && *cursor) {
        const char *start;
        size_t length;
        while (*cursor && (isspace((unsigned char)*cursor)
               || *cursor == ',' || *cursor == ';')) cursor++;
        start = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)
               && *cursor != ',' && *cursor != ';') cursor++;
        length = (size_t)(cursor - start);
        if (length == token_length && _strnicmp(start, token, length) == 0)
            return true;
    }
    return false;
}

bool character_trait_list_toggle(char *list, size_t size, const char *token)
{
    CharBuffer output = {0};
    const char *cursor = list;
    bool removing = character_trait_list_contains(list, token);
    while (cursor && *cursor) {
        const char *start;
        size_t length;
        while (*cursor && (isspace((unsigned char)*cursor)
               || *cursor == ',' || *cursor == ';')) cursor++;
        start = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)
               && *cursor != ',' && *cursor != ';') cursor++;
        length = (size_t)(cursor - start);
        if (!length) continue;
        if (removing && length == strlen(token)
            && _strnicmp(start, token, length) == 0) continue;
        if (output.length && !buffer_add(&output, " ")) goto failure;
        if (!buffer_add_n(&output, start, length)) goto failure;
    }
    if (!removing) {
        if (output.length && !buffer_add(&output, " ")) goto failure;
        if (!buffer_add(&output, token)) goto failure;
    }
    if (output.length + 1 > size) goto failure;
    snprintf(list, size, "%s", output.data ? output.data : "");
    free(output.data);
    return true;
failure:
    free(output.data);
    return false;
}

static bool catalog_add(CharacterTraitCatalog *catalog,
                        const char *token, size_t token_length,
                        uint32_t roles)
{
    size_t i;
    if (!token_length || token_length >= sizeof(catalog->items[0].token)
        || token[0] == '@') return true;
    for (i = 0; i < catalog->count; ++i) {
        if (strlen(catalog->items[i].token) == token_length
            && _strnicmp(catalog->items[i].token, token, token_length) == 0) {
            catalog->items[i].roles |= roles;
            return true;
        }
    }
    if (catalog->count == catalog->capacity) {
        size_t capacity = catalog->capacity ? catalog->capacity * 2 : 512;
        CharacterTrait *grown =
            realloc(catalog->items, capacity * sizeof(*grown));
        if (!grown) return false;
        catalog->items = grown;
        catalog->capacity = capacity;
    }
    memset(&catalog->items[catalog->count], 0,
           sizeof(catalog->items[catalog->count]));
    memcpy(catalog->items[catalog->count].token, token, token_length);
    catalog->items[catalog->count].token[token_length] = '\0';
    catalog->items[catalog->count].roles = roles;
    catalog->count++;
    return true;
}

static bool collect_root_traits(CharacterTraitCatalog *catalog,
                                const char *text, const char *root_name,
                                uint32_t roles)
{
    size_t i = 0;
    int depth = 0;
    int root_depth = -1;
    char pending[128] = "";
    int pending_depth = -1;
    bool has_equals = false;
    while (text[i]) {
        if (isspace((unsigned char)text[i])) {
            i++;
            continue;
        }
        if (text[i] == '#') {
            while (text[i] && text[i] != '\n') i++;
            continue;
        }
        if (text[i] == '"') {
            i++;
            while (text[i] && text[i] != '"') {
                if (text[i] == '\\' && text[i + 1]) i += 2;
                else i++;
            }
            if (text[i]) i++;
            pending[0] = '\0';
            has_equals = false;
            continue;
        }
        if (text[i] == '=') {
            has_equals = pending[0] != '\0';
            i++;
            continue;
        }
        if (text[i] == '{') {
            if (has_equals && pending[0]) {
                if (!root_name[0] && depth == 0) {
                    if (!catalog_add(catalog, pending, strlen(pending), roles))
                        return false;
                } else if (root_depth < 0 && depth == 0
                    && _stricmp(pending, root_name) == 0) {
                    root_depth = depth + 1;
                } else if (root_depth >= 0 && depth == root_depth
                           && pending_depth == depth) {
                    if (!catalog_add(catalog, pending, strlen(pending), roles))
                        return false;
                }
            }
            depth++;
            pending[0] = '\0';
            has_equals = false;
            i++;
            continue;
        }
        if (text[i] == '}') {
            depth--;
            if (root_depth >= 0 && depth < root_depth) root_depth = -1;
            pending[0] = '\0';
            has_equals = false;
            i++;
            continue;
        }
        {
            size_t start = i;
            size_t length;
            while (text[i] && !isspace((unsigned char)text[i])
                   && text[i] != '#' && text[i] != '"' && text[i] != '='
                   && text[i] != '{' && text[i] != '}') i++;
            length = i - start;
            if (length < sizeof(pending)) {
                memcpy(pending, text + start, length);
                pending[length] = '\0';
                pending_depth = depth;
                has_equals = false;
            } else {
                pending[0] = '\0';
                has_equals = false;
            }
        }
    }
    return true;
}

typedef struct {
    CharacterTraitCatalog *catalog;
    const char *root_name;
    uint32_t roles;
} TraitVisitorContext;

static bool trait_file_visitor(const char *path, const char *name, void *user)
{
    TraitVisitorContext *context = user;
    char error[256] = "";
    char *text = read_text_file(path, error, sizeof(error));
    (void)name;
    if (!text) return true;
    if (!collect_root_traits(context->catalog, text,
                             context->root_name, context->roles)) {
        free(text);
        snprintf(context->catalog->error, sizeof(context->catalog->error),
                 "Mémoire insuffisante pendant le chargement des traits.");
        return false;
    }
    free(text);
    return true;
}

static bool load_trait_directory(CharacterTraitCatalog *catalog,
                                 const char *root, const char *relative,
                                 const char *root_name, uint32_t roles)
{
    char directory[CP_PATH_MAX];
    TraitVisitorContext context = {catalog, root_name, roles};
    if (!root || !root[0]
        || !cp_path_join(directory, sizeof(directory), root, relative)
        || !cp_path_is_dir(directory)) return true;
    return cp_visit_txt_files(directory, trait_file_visitor, &context)
        && !catalog->error[0];
}

static int compare_traits(const void *left, const void *right)
{
    const CharacterTrait *a = left;
    const CharacterTrait *b = right;
    return _stricmp(a->token, b->token);
}

void character_trait_catalog_free(CharacterTraitCatalog *catalog)
{
    if (!catalog) return;
    free(catalog->items);
    memset(catalog, 0, sizeof(*catalog));
}

bool character_trait_catalog_load(CharacterTraitCatalog *catalog,
                                  const char *game_root, const char *mod_root)
{
    uint32_t country_roles =
        CHARACTER_ROLE_COUNTRY_LEADER | CHARACTER_ROLE_ADVISOR;
    uint32_t unit_roles = CHARACTER_ROLE_GENERAL
        | CHARACTER_ROLE_FIELD_MARSHAL | CHARACTER_ROLE_NAVY_LEADER;
    character_trait_catalog_free(catalog);
    if (!load_trait_directory(catalog, game_root, "common\\country_leader",
                              "leader_traits", country_roles)
        || !load_trait_directory(catalog, game_root, "common\\unit_leader",
                                 "leader_traits", unit_roles)
        || !load_trait_directory(catalog, game_root, "common\\scientist_traits",
                                 "", CHARACTER_ROLE_SCIENTIST)
        || !load_trait_directory(catalog, mod_root, "common\\country_leader",
                                 "leader_traits", country_roles)
        || !load_trait_directory(catalog, mod_root, "common\\unit_leader",
                                 "leader_traits", unit_roles)
        || !load_trait_directory(catalog, mod_root, "common\\scientist_traits",
                                 "", CHARACTER_ROLE_SCIENTIST)) {
        if (!catalog->error[0])
            snprintf(catalog->error, sizeof(catalog->error),
                     "Impossible de charger le catalogue de traits.");
        return false;
    }
    qsort(catalog->items, catalog->count, sizeof(catalog->items[0]),
          compare_traits);
    return true;
}
