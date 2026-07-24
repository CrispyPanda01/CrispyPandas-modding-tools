#include "state_edit.h"

#include <windows.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

static bool buffer_append(Buffer *buffer, const char *data, size_t length)
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
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool valid_tag(const char *tag)
{
    size_t i;
    if (strlen(tag) != 3) return false;
    for (i = 0; i < 3; ++i) {
        if (!isalnum((unsigned char)tag[i])) return false;
    }
    return true;
}

static void uppercase(char *text)
{
    while (*text) {
        *text = (char)toupper((unsigned char)*text);
        text++;
    }
}

bool state_edit_build_request(StateEditRequest *request,
                              const char *owner,
                              const char *controller,
                              const char *cores,
                              bool keep_existing_cores,
                              char *error, size_t error_size)
{
    char list[1024];
    char *token;
    memset(request, 0, sizeof(*request));
    snprintf(request->owner, sizeof(request->owner), "%.7s", owner ? owner : "");
    snprintf(request->controller, sizeof(request->controller), "%.7s", controller ? controller : "");
    uppercase(request->owner);
    uppercase(request->controller);
    if (request->owner[0] && !valid_tag(request->owner)) {
        snprintf(error, error_size, "Owner invalide : utilise un tag de 3 caractères.");
        return false;
    }
    if (request->controller[0] && !valid_tag(request->controller)) {
        snprintf(error, error_size, "Controller invalide : utilise un tag de 3 caractères.");
        return false;
    }
    snprintf(list, sizeof(list), "%.1023s", cores ? cores : "");
    token = strtok(list, " ,;\t\r\n");
    while (token) {
        size_t i;
        uppercase(token);
        if (!valid_tag(token)) {
            snprintf(error, error_size, "Core invalide « %.16s » : utilise des tags de 3 caractères.", token);
            return false;
        }
        for (i = 0; i < request->core_count; ++i) {
            if (strcmp(request->cores[i], token) == 0) break;
        }
        if (i == request->core_count) {
            if (request->core_count >= STATE_EDIT_MAX_CORES) {
                snprintf(error, error_size, "Trop de cores (maximum %d).", STATE_EDIT_MAX_CORES);
                return false;
            }
            snprintf(request->cores[request->core_count++], 8, "%.7s", token);
        }
        token = strtok(NULL, " ,;\t\r\n");
    }
    request->keep_existing_cores = keep_existing_cores;
    if (!request->owner[0] && !request->controller[0] && request->core_count == 0) {
        snprintf(error, error_size, "Tous les champs sont vides : aucune modification à appliquer.");
        return false;
    }
    return true;
}

static bool line_has_key(const char *line, size_t length, const char *key)
{
    size_t i = 0, k = 0;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= length || line[i] == '#') return false;
    while (key[k] && i + k < length
           && tolower((unsigned char)line[i + k]) == tolower((unsigned char)key[k])) k++;
    if (key[k]) return false;
    i += k;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) i++;
    return i < length && line[i] == '=';
}

static int brace_delta(const char *line, size_t length)
{
    int delta = 0;
    bool quoted = false;
    size_t i;
    for (i = 0; i < length; ++i) {
        char c = line[i];
        if (!quoted && c == '#') break;
        if (c == '"' && (i == 0 || line[i - 1] != '\\')) quoted = !quoted;
        if (!quoted && c == '{') delta++;
        else if (!quoted && c == '}') delta--;
    }
    return delta;
}

static size_t leading_whitespace(const char *line, size_t length)
{
    size_t i = 0;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) i++;
    return i;
}

static bool append_property(Buffer *out, const char *indent, const char *key,
                            const char *value, const char *newline)
{
    char line[256];
    int length = snprintf(line, sizeof(line), "%s%s = %s%s", indent, key, value, newline);
    return length > 0 && buffer_append(out, line, (size_t)length);
}

static bool core_in_request(const StateEditRequest *request, const char *tag)
{
    size_t i;
    for (i = 0; i < request->core_count; ++i) {
        if (_stricmp(request->cores[i], tag) == 0) return true;
    }
    return false;
}

static bool extract_value(const char *line, size_t length, char *value, size_t value_size)
{
    const char *equals = memchr(line, '=', length);
    size_t i = 0;
    if (!equals) return false;
    equals++;
    while ((size_t)(equals - line) < length && isspace((unsigned char)*equals)) equals++;
    while ((size_t)(equals - line) < length && !isspace((unsigned char)*equals)
           && *equals != '#' && *equals != '}' && i + 1 < value_size) {
        value[i++] = *equals++;
    }
    value[i] = '\0';
    return i > 0;
}

bool state_edit_transform(const char *input, const StateEditRequest *request,
                          char **output, char *error, size_t error_size)
{
    const char *cursor = input;
    const char *history_line = NULL;
    size_t history_length = 0;
    int depth = 0;
    bool in_history = false;
    bool owner_found = false, controller_found = false;
    bool had_owner, had_controller;
    char indent[64] = "\t\t";
    const char *newline = strstr(input, "\r\n") ? "\r\n" : "\n";
    Buffer out = {0};

    /* First pass: find the history block, direct properties and its indentation. */
    while (*cursor) {
        const char *end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor + 1) : strlen(cursor);
        size_t content_length = length;
        while (content_length && (cursor[content_length - 1] == '\r' || cursor[content_length - 1] == '\n'))
            content_length--;
        if (!in_history && line_has_key(cursor, content_length, "history")
            && memchr(cursor, '{', content_length)) {
            in_history = true;
            history_line = cursor;
            history_length = length;
            depth = brace_delta(cursor, content_length);
        } else if (in_history) {
            size_t whitespace = leading_whitespace(cursor, content_length);
            if (depth == 1 && content_length > whitespace && cursor[whitespace] != '#'
                && cursor[whitespace] != '}') {
                if (whitespace > 0 && whitespace < sizeof(indent)) {
                    memcpy(indent, cursor, whitespace);
                    indent[whitespace] = '\0';
                }
                if (line_has_key(cursor, content_length, "owner")) owner_found = true;
                if (line_has_key(cursor, content_length, "controller")) controller_found = true;
            }
            depth += brace_delta(cursor, content_length);
            if (depth <= 0) in_history = false;
        }
        cursor += length;
    }
    if (!history_line) {
        snprintf(error, error_size, "Le fichier ne contient aucun bloc history éditable.");
        return false;
    }
    had_owner = owner_found;
    had_controller = controller_found;

    cursor = input;
    in_history = false;
    depth = 0;
    owner_found = controller_found = false;
    while (*cursor) {
        const char *end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor + 1) : strlen(cursor);
        size_t content_length = length;
        bool skip = false;
        while (content_length && (cursor[content_length - 1] == '\r' || cursor[content_length - 1] == '\n'))
            content_length--;
        if (cursor == history_line) {
            if (!buffer_append(&out, cursor, length)) goto memory_error;
            in_history = true;
            depth = brace_delta(cursor, content_length);
            if (request->owner[0] && !had_owner
                && !append_property(&out, indent, "owner", request->owner, newline)) goto memory_error;
            if (request->controller[0] && !had_controller
                && !append_property(&out, indent, "controller", request->controller, newline)) goto memory_error;
            if (request->core_count) {
                size_t i;
                for (i = 0; i < request->core_count; ++i) {
                    if (!append_property(&out, indent, "add_core_of", request->cores[i], newline))
                        goto memory_error;
                }
            }
            cursor += length;
            continue;
        }
        if (in_history) {
            if (depth == 1 && line_has_key(cursor, content_length, "owner") && request->owner[0]) {
                if (!owner_found) {
                    size_t whitespace = leading_whitespace(cursor, content_length);
                    char prefix[64];
                    if (whitespace >= sizeof(prefix)) whitespace = sizeof(prefix) - 1;
                    memcpy(prefix, cursor, whitespace); prefix[whitespace] = '\0';
                    if (!append_property(&out, prefix, "owner", request->owner, newline)) goto memory_error;
                    owner_found = true;
                }
                skip = true;
            } else if (depth == 1 && line_has_key(cursor, content_length, "controller")
                       && request->controller[0]) {
                if (!controller_found) {
                    size_t whitespace = leading_whitespace(cursor, content_length);
                    char prefix[64];
                    if (whitespace >= sizeof(prefix)) whitespace = sizeof(prefix) - 1;
                    memcpy(prefix, cursor, whitespace); prefix[whitespace] = '\0';
                    if (!append_property(&out, prefix, "controller", request->controller, newline))
                        goto memory_error;
                    controller_found = true;
                }
                skip = true;
            } else if (line_has_key(cursor, content_length, "add_core_of")
                       && request->core_count) {
                if (!request->keep_existing_cores) {
                    skip = true;
                } else {
                    char tag[16];
                    if (extract_value(cursor, content_length, tag, sizeof(tag))
                        && core_in_request(request, tag)) skip = true;
                }
            }
            depth += brace_delta(cursor, content_length);
            if (depth <= 0) in_history = false;
        }
        if (!skip && !buffer_append(&out, cursor, length)) goto memory_error;
        cursor += length;
    }
    *output = out.data;
    (void)history_length;
    return true;

memory_error:
    free(out.data);
    snprintf(error, error_size, "Mémoire insuffisante pendant l'édition.");
    return false;
}

static char *read_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *text;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END); size = ftell(file); rewind(file);
    if (size < 0 || !(text = malloc((size_t)size + 1))) { fclose(file); return NULL; }
    if (fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text); fclose(file); return NULL;
    }
    fclose(file); text[size] = '\0'; return text;
}

bool state_edit_file(const char *source, const char *mod_root,
                     const StateEditRequest *request,
                     char *target, size_t target_size,
                     char *error, size_t error_size)
{
    const char *filename;
    char history_dir[CP_PATH_MAX], states_dir[CP_PATH_MAX], temp[CP_PATH_MAX];
    char *input, *output;
    FILE *file;
    filename = strrchr(source, '\\');
    if (!filename) filename = strrchr(source, '/');
    filename = filename ? filename + 1 : source;
    if (!cp_path_join(history_dir, sizeof(history_dir), mod_root, "history")
        || !cp_path_join(states_dir, sizeof(states_dir), history_dir, "states")
        || !cp_path_join(target, target_size, states_dir, filename)) {
        snprintf(error, error_size, "Chemin de fichier trop long.");
        return false;
    }
    if ((!CreateDirectoryA(history_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        || (!CreateDirectoryA(states_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)) {
        snprintf(error, error_size, "Impossible de créer le dossier history\\states du mod.");
        return false;
    }
    input = read_text(source);
    if (!input) {
        snprintf(error, error_size, "Impossible de lire %.400s", source);
        return false;
    }
    if (!state_edit_transform(input, request, &output, error, error_size)) {
        free(input);
        return false;
    }
    free(input);
    snprintf(temp, sizeof(temp), "%.4080s.crispy.tmp", target);
    file = fopen(temp, "wb");
    if (!file) {
        free(output);
        snprintf(error, error_size, "Impossible d'écrire %.400s", target);
        return false;
    }
    if (fwrite(output, 1, strlen(output), file) != strlen(output)) {
        fclose(file);
        DeleteFileA(temp);
        free(output);
        snprintf(error, error_size, "Impossible d'écrire %.400s", target);
        return false;
    }
    free(output);
    if (fclose(file) != 0) {
        DeleteFileA(temp);
        snprintf(error, error_size, "Impossible de finaliser %.400s", target);
        return false;
    }
    if (!MoveFileExA(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temp);
        snprintf(error, error_size, "Impossible de remplacer %.400s", target);
        return false;
    }
    return true;
}
