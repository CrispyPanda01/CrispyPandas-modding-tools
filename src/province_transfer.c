#include "province_transfer.h"

#include <windows.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "path_utils.h"

typedef enum { PT_WORD, PT_STRING, PT_EQUALS, PT_OPEN, PT_CLOSE } PtTokenType;

typedef struct {
    PtTokenType type;
    size_t start;
    size_t end;
    int depth;
} PtToken;

typedef struct {
    PtToken *items;
    size_t count;
    size_t capacity;
} TokenList;

typedef struct {
    size_t start;
    size_t end;
    char *replacement;
} TextEdit;

typedef struct {
    TextEdit *items;
    size_t count;
    size_t capacity;
} EditList;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

typedef struct {
    size_t key_token;
    size_t open_token;
    size_t close_token;
} PtBlock;

typedef struct {
    int province;
    char value[96];
    char *comments;
} VpAsset;

typedef struct {
    int province;
    char *block;
} BuildingAsset;

typedef struct {
    VpAsset *vps;
    size_t vp_count;
    size_t vp_capacity;
    BuildingAsset *buildings;
    size_t building_count;
    size_t building_capacity;
} AssetSet;

typedef struct {
    const char *text;
    TokenList tokens;
    PtBlock history;
    PtBlock provinces;
    bool has_history;
    bool has_provinces;
    PtBlock buildings;
    bool has_buildings;
    PtBlock *vp_blocks;
    size_t vp_block_count;
    size_t vp_block_capacity;
    AssetSet assets;
} StateDocument;

typedef struct {
    char source[CP_PATH_MAX];
    char target[CP_PATH_MAX];
    char *content;
    char temp[CP_PATH_MAX];
    char backup[CP_PATH_MAX];
    bool had_original;
    bool committed;
} FileChange;

typedef struct {
    FileChange *items;
    size_t count;
    size_t capacity;
} ChangeList;

static bool buffer_append_n(TextBuffer *buffer, const char *text, size_t length)
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

static bool buffer_append(TextBuffer *buffer, const char *text)
{
    return buffer_append_n(buffer, text, strlen(text));
}

static bool token_push(TokenList *tokens, PtToken token)
{
    if (tokens->count == tokens->capacity) {
        size_t capacity = tokens->capacity ? tokens->capacity * 2 : 1024;
        PtToken *grown = realloc(tokens->items, capacity * sizeof(*grown));
        if (!grown) return false;
        tokens->items = grown;
        tokens->capacity = capacity;
    }
    tokens->items[tokens->count++] = token;
    return true;
}

static bool tokenize(const char *text, TokenList *tokens, char *error, size_t error_size)
{
    size_t i = 0;
    int depth = 0;
    while (text[i]) {
        PtToken token;
        if (isspace((unsigned char)text[i])) { i++; continue; }
        if (text[i] == '#') {
            while (text[i] && text[i] != '\n') i++;
            continue;
        }
        token.start = i;
        token.depth = depth;
        if (text[i] == '"') {
            token.type = PT_STRING;
            i++;
            while (text[i] && text[i] != '"') {
                if (text[i] == '\\' && text[i + 1]) i += 2;
                else i++;
            }
            if (!text[i]) {
                snprintf(error, error_size, "Chaîne de caractères non terminée.");
                free(tokens->items); memset(tokens, 0, sizeof(*tokens)); return false;
            }
            i++;
        } else if (text[i] == '=') {
            token.type = PT_EQUALS; i++;
        } else if (text[i] == '{') {
            token.type = PT_OPEN; i++; depth++;
        } else if (text[i] == '}') {
            if (depth <= 0) {
                snprintf(error, error_size, "Accolade fermante sans ouverture.");
                free(tokens->items); memset(tokens, 0, sizeof(*tokens)); return false;
            }
            depth--;
            token.depth = depth;
            token.type = PT_CLOSE; i++;
        } else {
            token.type = PT_WORD;
            while (text[i] && !isspace((unsigned char)text[i])
                   && text[i] != '#' && text[i] != '"' && text[i] != '='
                   && text[i] != '{' && text[i] != '}') i++;
        }
        token.end = i;
        if (!token_push(tokens, token)) {
            snprintf(error, error_size, "Mémoire insuffisante pendant l'analyse.");
            free(tokens->items); memset(tokens, 0, sizeof(*tokens)); return false;
        }
    }
    if (depth != 0) {
        snprintf(error, error_size, "Bloc non fermé : %d accolade(s) manquante(s).", depth);
        free(tokens->items); memset(tokens, 0, sizeof(*tokens)); return false;
    }
    return true;
}

bool province_transfer_validate_syntax(const char *text, char *error, size_t error_size)
{
    TokenList tokens = {0};
    bool ok = tokenize(text, &tokens, error, error_size);
    free(tokens.items);
    return ok;
}

static bool token_equals(const char *text, const PtToken *token, const char *word)
{
    size_t length = token->end - token->start;
    return token->type == PT_WORD && strlen(word) == length
        && _strnicmp(text + token->start, word, length) == 0;
}

static int token_integer(const char *text, const PtToken *token)
{
    char value[32];
    size_t length = token->end - token->start;
    if (length >= sizeof(value)) return -1;
    memcpy(value, text + token->start, length);
    value[length] = '\0';
    for (size_t i = 0; i < length; ++i) if (!isdigit((unsigned char)value[i])) return -1;
    return atoi(value);
}

static size_t matching_close(const TokenList *tokens, size_t open_index)
{
    int depth;
    size_t i;
    if (open_index >= tokens->count || tokens->items[open_index].type != PT_OPEN) return (size_t)-1;
    depth = tokens->items[open_index].depth;
    for (i = open_index + 1; i < tokens->count; ++i) {
        if (tokens->items[i].type == PT_CLOSE && tokens->items[i].depth == depth) return i;
    }
    return (size_t)-1;
}

static bool assignment_block_at(const char *text, const TokenList *tokens,
                                size_t key_index, PtBlock *block)
{
    size_t close;
    if (key_index + 2 >= tokens->count
        || tokens->items[key_index].type != PT_WORD
        || tokens->items[key_index + 1].type != PT_EQUALS
        || tokens->items[key_index + 2].type != PT_OPEN) return false;
    close = matching_close(tokens, key_index + 2);
    if (close == (size_t)-1) return false;
    block->key_token = key_index;
    block->open_token = key_index + 2;
    block->close_token = close;
    (void)text;
    return true;
}

static bool find_named_block(const char *text, const TokenList *tokens,
                             const char *name, int required_depth, PtBlock *block)
{
    size_t i;
    for (i = 0; i + 2 < tokens->count; ++i) {
        if ((required_depth < 0 || tokens->items[i].depth == required_depth)
            && token_equals(text, &tokens->items[i], name)
            && assignment_block_at(text, tokens, i, block)) return true;
    }
    return false;
}

static size_t line_start(const char *text, size_t position)
{
    while (position > 0 && text[position - 1] != '\n') position--;
    return position;
}

static size_t line_end(const char *text, size_t position)
{
    while (text[position] && text[position] != '\n') position++;
    if (text[position] == '\n') position++;
    return position;
}

static void line_indent(const char *text, size_t position, char *indent, size_t size)
{
    size_t start = line_start(text, position);
    size_t n = 0;
    while (start < position && (text[start] == ' ' || text[start] == '\t') && n + 1 < size)
        indent[n++] = text[start++];
    indent[n] = '\0';
}

static const char *newline_of(const char *text)
{
    return strstr(text, "\r\n") ? "\r\n" : "\n";
}

static char *comments_in_range(const char *text, size_t start, size_t end)
{
    TextBuffer comments = {0};
    size_t i = start;
    bool quoted = false;
    while (i < end) {
        if (text[i] == '"' && (i == 0 || text[i - 1] != '\\')) {
            quoted = !quoted; i++; continue;
        }
        if (!quoted && text[i] == '#') {
            size_t comment_start = i;
            while (i < end && text[i] != '\n' && text[i] != '\r') i++;
            while (i > comment_start + 1 && isspace((unsigned char)text[i - 1])) i--;
            if (!buffer_append_n(&comments, text + comment_start, i - comment_start)
                || !buffer_append(&comments, "\n")) {
                free(comments.data); return NULL;
            }
            while (i < end && (text[i] == '\r' || text[i] == '\n')) i++;
        } else {
            i++;
        }
    }
    if (!comments.data) {
        comments.data = malloc(1);
        if (comments.data) comments.data[0] = '\0';
    }
    return comments.data;
}

static bool edit_push(EditList *edits, size_t start, size_t end, char *replacement)
{
    if (edits->count == edits->capacity) {
        size_t capacity = edits->capacity ? edits->capacity * 2 : 16;
        TextEdit *grown = realloc(edits->items, capacity * sizeof(*grown));
        if (!grown) return false;
        edits->items = grown;
        edits->capacity = capacity;
    }
    edits->items[edits->count++] = (TextEdit){start, end, replacement};
    return true;
}

static int compare_edits(const void *left, const void *right)
{
    const TextEdit *a = left, *b = right;
    if (a->start < b->start) return -1;
    if (a->start > b->start) return 1;
    /*
     * An insertion may legitimately share its position with a replacement
     * that starts there (for example, regenerated VPs immediately after the
     * history opening). It must be applied first while the input cursor is
     * still at that boundary. qsort is not stable, so make this explicit.
     */
    if (a->start == a->end && b->start != b->end) return -1;
    if (a->start != a->end && b->start == b->end) return 1;
    if (a->end < b->end) return -1;
    if (a->end > b->end) return 1;
    return 0;
}

static bool apply_edits(const char *input, EditList *edits, char **output,
                        char *error, size_t error_size)
{
    TextBuffer result = {0};
    size_t cursor = 0, i;
    qsort(edits->items, edits->count, sizeof(edits->items[0]), compare_edits);
    for (i = 0; i < edits->count; ++i) {
        TextEdit *edit = &edits->items[i];
        if (edit->start < cursor || edit->end < edit->start) {
            snprintf(error, error_size, "Conflit interne entre deux modifications.");
            free(result.data); return false;
        }
        if (!buffer_append_n(&result, input + cursor, edit->start - cursor)
            || (edit->replacement && !buffer_append(&result, edit->replacement))) {
            snprintf(error, error_size, "Mémoire insuffisante pendant la modification.");
            free(result.data); return false;
        }
        cursor = edit->end;
    }
    if (!buffer_append(&result, input + cursor)) {
        snprintf(error, error_size, "Mémoire insuffisante pendant la modification.");
        free(result.data); return false;
    }
    *output = result.data;
    return true;
}

static void edits_free(EditList *edits)
{
    size_t i;
    for (i = 0; i < edits->count; ++i) free(edits->items[i].replacement);
    free(edits->items);
    memset(edits, 0, sizeof(*edits));
}

static char *build_id_block_inner(const char *input, const PtToken *open,
                                  const PtToken *close, const int *ids, size_t id_count)
{
    TextBuffer result = {0};
    char close_indent[64], item_indent[72], number[32];
    char *comments = comments_in_range(input, open->end, close->start);
    const char *newline = newline_of(input);
    size_t i;
    if (!comments) return NULL;
    line_indent(input, close->start, close_indent, sizeof(close_indent));
    snprintf(item_indent, sizeof(item_indent), "%s\t", close_indent);
    if (!buffer_append(&result, newline)) goto failed;
    if (comments[0]) {
        char *cursor = comments;
        while (*cursor) {
            char *end = strchr(cursor, '\n');
            size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
            if (!buffer_append(&result, item_indent)
                || !buffer_append_n(&result, cursor, length)
                || !buffer_append(&result, newline)) goto failed;
            cursor += length + (end ? 1 : 0);
        }
    }
    for (i = 0; i < id_count; ++i) {
        if (i % 20 == 0 && !buffer_append(&result, item_indent)) goto failed;
        snprintf(number, sizeof(number), "%d%s", ids[i],
                 (i + 1 == id_count || i % 20 == 19) ? "" : " ");
        if (!buffer_append(&result, number)) goto failed;
        if (i + 1 == id_count || i % 20 == 19) {
            if (!buffer_append(&result, newline)) goto failed;
        }
    }
    if (id_count == 0) {
        if (!buffer_append(&result, item_indent) || !buffer_append(&result, newline)) goto failed;
    }
    if (!buffer_append(&result, close_indent)) goto failed;
    free(comments);
    return result.data;
failed:
    free(comments); free(result.data); return NULL;
}

static bool id_block_matches(const char *text, const char *block_name,
                             const int *expected, size_t expected_count,
                             char *error, size_t error_size)
{
    TokenList tokens = {0};
    PtBlock block;
    int depth;
    size_t i, count = 0;
    bool ok = false;
    if (!tokenize(text, &tokens, error, error_size)) return false;
    if (!find_named_block(text, &tokens, block_name, -1, &block)) {
        snprintf(error, error_size, "Bloc « %s » absent après transformation.", block_name);
        goto cleanup;
    }
    depth = tokens.items[block.open_token].depth + 1;
    for (i = block.open_token + 1; i < block.close_token; ++i) {
        int id;
        if (tokens.items[i].depth != depth || tokens.items[i].type != PT_WORD) continue;
        id = token_integer(text, &tokens.items[i]);
        if (id <= 0) {
            snprintf(error, error_size, "Valeur invalide dans le bloc « %s ».", block_name);
            goto cleanup;
        }
        if (count >= expected_count || id != expected[count]) {
            snprintf(error, error_size, "Contenu inattendu dans le bloc « %s ».", block_name);
            goto cleanup;
        }
        count++;
    }
    if (count != expected_count) {
        snprintf(error, error_size, "Le bloc « %s » contient %zu IDs au lieu de %zu.",
                 block_name, count, expected_count);
        goto cleanup;
    }
    ok = true;
cleanup:
    free(tokens.items);
    return ok;
}

bool province_transfer_replace_id_block(const char *input, const char *block_name,
                                        const int *ids, size_t id_count,
                                        char **output,
                                        char *error, size_t error_size)
{
    TokenList tokens = {0};
    PtBlock block;
    EditList edits = {0};
    char *replacement;
    bool ok;
    if (!tokenize(input, &tokens, error, error_size)) return false;
    if (!find_named_block(input, &tokens, block_name, -1, &block)) {
        snprintf(error, error_size, "Bloc « %s » introuvable.", block_name);
        free(tokens.items); return false;
    }
    replacement = build_id_block_inner(input, &tokens.items[block.open_token],
                                       &tokens.items[block.close_token], ids, id_count);
    if (!replacement
        || !edit_push(&edits, tokens.items[block.open_token].end,
                      tokens.items[block.close_token].start, replacement)) {
        free(replacement); free(tokens.items);
        snprintf(error, error_size, "Mémoire insuffisante.");
        return false;
    }
    ok = apply_edits(input, &edits, output, error, error_size);
    edits_free(&edits);
    free(tokens.items);
    if (!ok) return false;
    if (!province_transfer_validate_syntax(*output, error, error_size)
        || !id_block_matches(*output, block_name, ids, id_count, error, error_size)) {
        free(*output); *output = NULL; return false;
    }
    return true;
}

static bool asset_add_vp(AssetSet *assets, int province, const char *value, char *comments)
{
    if (assets->vp_count == assets->vp_capacity) {
        size_t capacity = assets->vp_capacity ? assets->vp_capacity * 2 : 8;
        VpAsset *grown = realloc(assets->vps, capacity * sizeof(*grown));
        if (!grown) return false;
        assets->vps = grown;
        assets->vp_capacity = capacity;
    }
    assets->vps[assets->vp_count].province = province;
    snprintf(assets->vps[assets->vp_count].value,
             sizeof(assets->vps[assets->vp_count].value), "%.95s", value);
    assets->vps[assets->vp_count].comments = comments;
    assets->vp_count++;
    return true;
}

static bool asset_add_building(AssetSet *assets, int province, char *block)
{
    if (assets->building_count == assets->building_capacity) {
        size_t capacity = assets->building_capacity ? assets->building_capacity * 2 : 8;
        BuildingAsset *grown = realloc(assets->buildings, capacity * sizeof(*grown));
        if (!grown) return false;
        assets->buildings = grown;
        assets->building_capacity = capacity;
    }
    assets->buildings[assets->building_count++] = (BuildingAsset){province, block};
    return true;
}

static void assets_free(AssetSet *assets)
{
    size_t i;
    for (i = 0; i < assets->vp_count; ++i) free(assets->vps[i].comments);
    for (i = 0; i < assets->building_count; ++i) free(assets->buildings[i].block);
    free(assets->vps);
    free(assets->buildings);
    memset(assets, 0, sizeof(*assets));
}

static bool vp_block_push(StateDocument *document, PtBlock block)
{
    if (document->vp_block_count == document->vp_block_capacity) {
        size_t capacity = document->vp_block_capacity ? document->vp_block_capacity * 2 : 8;
        PtBlock *grown = realloc(document->vp_blocks, capacity * sizeof(*grown));
        if (!grown) return false;
        document->vp_blocks = grown;
        document->vp_block_capacity = capacity;
    }
    document->vp_blocks[document->vp_block_count++] = block;
    return true;
}

static char *span_copy(const char *text, size_t start, size_t end)
{
    char *copy;
    if (end < start) return NULL;
    copy = malloc(end - start + 1);
    if (!copy) return NULL;
    memcpy(copy, text + start, end - start);
    copy[end - start] = '\0';
    return copy;
}

static bool parse_vp_block(StateDocument *document, const PtBlock *block,
                           char *error, size_t error_size)
{
    const PtToken *open = &document->tokens.items[block->open_token];
    const PtToken *close = &document->tokens.items[block->close_token];
    char *comments = comments_in_range(document->text, open->end,
                                       line_end(document->text, close->end));
    size_t i = block->open_token + 1;
    bool comment_used = false;
    if (!comments) return false;
    while (i < block->close_token) {
        int province;
        const PtToken *value_token;
        char value[96];
        size_t length;
        if (document->tokens.items[i].depth != open->depth + 1
            || document->tokens.items[i].type != PT_WORD) { i++; continue; }
        province = token_integer(document->text, &document->tokens.items[i]);
        if (province <= 0 || i + 1 >= block->close_token) { i++; continue; }
        value_token = &document->tokens.items[i + 1];
        if (value_token->type != PT_WORD || value_token->depth != open->depth + 1) {
            snprintf(error, error_size, "Valeur de victory_points invalide pour la province %d.", province);
            free(comments); return false;
        }
        length = value_token->end - value_token->start;
        if (length >= sizeof(value)) length = sizeof(value) - 1;
        memcpy(value, document->text + value_token->start, length);
        value[length] = '\0';
        if (!asset_add_vp(&document->assets, province, value,
                          comment_used ? span_copy("", 0, 0) : comments)) {
            if (!comment_used) free(comments);
            return false;
        }
        comment_used = true;
        i += 2;
    }
    if (!comment_used) free(comments);
    return true;
}

static bool parse_state_document(const char *text, StateDocument *document,
                                 char *error, size_t error_size)
{
    size_t i;
    PtBlock root;
    int direct_depth;
    memset(document, 0, sizeof(*document));
    document->text = text;
    if (!tokenize(text, &document->tokens, error, error_size)) return false;
    if (!find_named_block(text, &document->tokens, "state", 0, &root)) {
        snprintf(error, error_size, "Bloc state principal introuvable.");
        return false;
    }
    direct_depth = document->tokens.items[root.open_token].depth + 1;
    document->has_history = find_named_block(text, &document->tokens, "history", direct_depth,
                                             &document->history);
    document->has_provinces = find_named_block(text, &document->tokens, "provinces", direct_depth,
                                               &document->provinces);
    if (!document->has_history || !document->has_provinces) {
        snprintf(error, error_size, "L'état ne contient pas les blocs history et provinces requis.");
        return false;
    }
    {
        direct_depth = document->tokens.items[document->history.open_token].depth + 1;
        for (i = document->history.open_token + 1; i < document->history.close_token; ++i) {
            PtBlock block;
            if (document->tokens.items[i].depth != direct_depth
                || document->tokens.items[i].type != PT_WORD
                || !assignment_block_at(text, &document->tokens, i, &block)) continue;
            if (token_equals(text, &document->tokens.items[i], "victory_points")) {
                if (!vp_block_push(document, block)
                    || !parse_vp_block(document, &block, error, error_size)) return false;
                i = block.close_token;
            } else if (token_equals(text, &document->tokens.items[i], "buildings")) {
                document->buildings = block;
                document->has_buildings = true;
            }
        }
    }
    if (document->has_buildings) {
        int child_depth = document->tokens.items[document->buildings.open_token].depth + 1;
        for (i = document->buildings.open_token + 1;
             i < document->buildings.close_token; ++i) {
            PtBlock block;
            int province;
            size_t start, end;
            char *raw;
            if (document->tokens.items[i].depth != child_depth
                || document->tokens.items[i].type != PT_WORD
                || !assignment_block_at(text, &document->tokens, i, &block)) continue;
            province = token_integer(text, &document->tokens.items[i]);
            if (province <= 0) continue;
            start = document->tokens.items[i].start;
            end = line_end(text, document->tokens.items[block.close_token].end);
            raw = span_copy(text, start, end);
            if (!raw || !asset_add_building(&document->assets, province, raw)) {
                free(raw); return false;
            }
            i = block.close_token;
        }
    }
    /* Detect selected assets hidden in dated/nested history during transfer later. */
    return true;
}

static void state_document_free(StateDocument *document)
{
    free(document->tokens.items);
    free(document->vp_blocks);
    assets_free(&document->assets);
    memset(document, 0, sizeof(*document));
}

static bool vp_exists(const AssetSet *assets, int province)
{
    size_t i;
    for (i = 0; i < assets->vp_count; ++i) if (assets->vps[i].province == province) return true;
    return false;
}

static bool id_in_list(int id, const int *ids, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) if (ids[i] == id) return true;
    return false;
}

static bool building_exists(const AssetSet *assets, int province)
{
    size_t i;
    for (i = 0; i < assets->building_count; ++i)
        if (assets->buildings[i].province == province) return true;
    return false;
}

static bool copy_asset_set_filtered(const AssetSet *source, const uint8_t *exclude,
                                    AssetSet *target)
{
    size_t i;
    for (i = 0; i < source->vp_count; ++i) {
        char *comments;
        if (exclude && exclude[source->vps[i].province]) continue;
        comments = span_copy(source->vps[i].comments ? source->vps[i].comments : "",
                             0, strlen(source->vps[i].comments ? source->vps[i].comments : ""));
        if (!comments || !asset_add_vp(target, source->vps[i].province,
                                       source->vps[i].value, comments)) {
            free(comments); return false;
        }
    }
    for (i = 0; i < source->building_count; ++i) {
        char *block;
        if (exclude && exclude[source->buildings[i].province]) continue;
        block = span_copy(source->buildings[i].block, 0, strlen(source->buildings[i].block));
        if (!block || !asset_add_building(target, source->buildings[i].province, block)) {
            free(block); return false;
        }
    }
    return true;
}

static bool append_assets(AssetSet *target, const AssetSet *incoming,
                          char *error, size_t error_size)
{
    size_t i;
    for (i = 0; i < incoming->vp_count; ++i) {
        char *comments;
        if (vp_exists(target, incoming->vps[i].province)) {
            snprintf(error, error_size,
                     "La province %d possède déjà un victory point dans l'état cible.",
                     incoming->vps[i].province);
            return false;
        }
        comments = span_copy(incoming->vps[i].comments ? incoming->vps[i].comments : "",
                             0, strlen(incoming->vps[i].comments ? incoming->vps[i].comments : ""));
        if (!comments || !asset_add_vp(target, incoming->vps[i].province,
                                       incoming->vps[i].value, comments)) {
            free(comments); return false;
        }
    }
    for (i = 0; i < incoming->building_count; ++i) {
        char *block;
        if (building_exists(target, incoming->buildings[i].province)) {
            snprintf(error, error_size,
                     "La province %d possède déjà un bloc de bâtiments dans l'état cible.",
                     incoming->buildings[i].province);
            return false;
        }
        block = span_copy(incoming->buildings[i].block, 0,
                          strlen(incoming->buildings[i].block));
        if (!block || !asset_add_building(target, incoming->buildings[i].province, block)) {
            free(block); return false;
        }
    }
    return true;
}

static bool collect_moved_assets(const AssetSet *source, const uint8_t *moved,
                                 AssetSet *out)
{
    size_t i;
    for (i = 0; i < source->vp_count; ++i) {
        char *comments;
        if (!moved[source->vps[i].province]) continue;
        comments = span_copy(source->vps[i].comments ? source->vps[i].comments : "",
                             0, strlen(source->vps[i].comments ? source->vps[i].comments : ""));
        if (!comments || !asset_add_vp(out, source->vps[i].province,
                                       source->vps[i].value, comments)) {
            free(comments); return false;
        }
    }
    for (i = 0; i < source->building_count; ++i) {
        char *block;
        if (!moved[source->buildings[i].province]) continue;
        block = span_copy(source->buildings[i].block, 0, strlen(source->buildings[i].block));
        if (!block || !asset_add_building(out, source->buildings[i].province, block)) {
            free(block); return false;
        }
    }
    return true;
}

static bool block_contains_selected_id(const StateDocument *document,
                                       const PtBlock *block,
                                       const uint8_t *selected)
{
    size_t i;
    int inner_depth = document->tokens.items[block->open_token].depth + 1;
    for (i = block->open_token + 1; i < block->close_token; ++i) {
        int id;
        if (document->tokens.items[i].depth != inner_depth
            || document->tokens.items[i].type != PT_WORD) continue;
        id = token_integer(document->text, &document->tokens.items[i]);
        if (id > 0 && id < HOI4_MAX_PROVINCES && selected[id]) return true;
    }
    return false;
}

static bool check_unsupported_nested_assets(const StateDocument *document,
                                            const uint8_t *selected,
                                            char *error, size_t error_size)
{
    size_t i;
    int direct_depth = document->tokens.items[document->history.open_token].depth + 1;
    for (i = document->history.open_token + 1; i < document->history.close_token; ++i) {
        PtBlock block;
        if (document->tokens.items[i].type != PT_WORD
            || !assignment_block_at(document->text, &document->tokens, i, &block)) continue;
        if (token_equals(document->text, &document->tokens.items[i], "victory_points")
            && document->tokens.items[i].depth != direct_depth
            && block_contains_selected_id(document, &block, selected)) {
            snprintf(error, error_size,
                     "Une province sélectionnée possède un victory point dans un bloc daté/imbriqué; transfert annulé.");
            return false;
        }
        if (token_equals(document->text, &document->tokens.items[i], "buildings")
            && document->tokens.items[i].depth != direct_depth) {
            size_t j;
            int child_depth = document->tokens.items[block.open_token].depth + 1;
            for (j = block.open_token + 1; j < block.close_token; ++j) {
                PtBlock child;
                int province;
                if (document->tokens.items[j].depth != child_depth
                    || !assignment_block_at(document->text, &document->tokens, j, &child)) continue;
                province = token_integer(document->text, &document->tokens.items[j]);
                if (province > 0 && province < HOI4_MAX_PROVINCES && selected[province]) {
                    snprintf(error, error_size,
                             "Une province sélectionnée possède des bâtiments dans un bloc daté/imbriqué; transfert annulé.");
                    return false;
                }
            }
        }
    }
    return true;
}

static bool append_comment_lines(TextBuffer *buffer, const char *comments,
                                 const char *indent, const char *newline)
{
    const char *cursor = comments;
    while (cursor && *cursor) {
        const char *end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length && (!buffer_append(buffer, indent)
                       || !buffer_append_n(buffer, cursor, length)
                       || !buffer_append(buffer, newline))) return false;
        cursor += length + (end ? 1 : 0);
    }
    return true;
}

static bool build_vp_text(const AssetSet *assets, const char *indent,
                          const char *newline, TextBuffer *buffer)
{
    size_t i;
    char line[256];
    for (i = 0; i < assets->vp_count; ++i) {
        if (!append_comment_lines(buffer, assets->vps[i].comments, indent, newline)) return false;
        snprintf(line, sizeof(line), "%svictory_points = { %d %s }%s",
                 indent, assets->vps[i].province, assets->vps[i].value, newline);
        if (!buffer_append(buffer, line)) return false;
    }
    return true;
}

static bool build_building_text(const AssetSet *assets, const char *indent,
                                const char *newline, TextBuffer *buffer)
{
    size_t i;
    for (i = 0; i < assets->building_count; ++i) {
        size_t length = strlen(assets->buildings[i].block);
        if (!buffer_append(buffer, indent)
            || !buffer_append(buffer, assets->buildings[i].block)) return false;
        if (length == 0 || (assets->buildings[i].block[length - 1] != '\n'
                            && assets->buildings[i].block[length - 1] != '\r')) {
            if (!buffer_append(buffer, newline)) return false;
        }
    }
    return true;
}

static bool line_prefix_whitespace(const char *text, size_t position)
{
    size_t start = line_start(text, position);
    while (start < position) {
        if (text[start] != ' ' && text[start] != '\t') return false;
        start++;
    }
    return true;
}

static bool validate_state_output(const char *text,
                                  const int *final_provinces, size_t final_count,
                                  const AssetSet *expected_assets,
                                  char *error, size_t error_size)
{
    StateDocument verify;
    size_t i, j;
    if (!id_block_matches(text, "provinces", final_provinces, final_count,
                          error, error_size)) return false;
    if (!parse_state_document(text, &verify, error, error_size)) {
        state_document_free(&verify);
        return false;
    }
    if (verify.assets.vp_count != expected_assets->vp_count
        || verify.assets.building_count != expected_assets->building_count) {
        snprintf(error, error_size,
                 "Le nombre de VPs ou blocs de bâtiments a changé de façon inattendue.");
        state_document_free(&verify);
        return false;
    }
    for (i = 0; i < verify.assets.vp_count; ++i) {
        bool found = false;
        if (!id_in_list(verify.assets.vps[i].province, final_provinces, final_count)) {
            snprintf(error, error_size, "VP provincial %d hors de son état.",
                     verify.assets.vps[i].province);
            state_document_free(&verify);
            return false;
        }
        for (j = 0; j < expected_assets->vp_count; ++j) {
            if (verify.assets.vps[i].province == expected_assets->vps[j].province
                && strcmp(verify.assets.vps[i].value, expected_assets->vps[j].value) == 0) {
                found = true; break;
            }
        }
        if (!found) {
            snprintf(error, error_size, "Victory point %d altéré pendant le transfert.",
                     verify.assets.vps[i].province);
            state_document_free(&verify);
            return false;
        }
    }
    for (i = 0; i < verify.assets.building_count; ++i) {
        if (!id_in_list(verify.assets.buildings[i].province, final_provinces, final_count)
            || !building_exists(expected_assets, verify.assets.buildings[i].province)) {
            snprintf(error, error_size, "Bloc de bâtiments provincial %d invalide après transfert.",
                     verify.assets.buildings[i].province);
            state_document_free(&verify);
            return false;
        }
    }
    state_document_free(&verify);
    return true;
}

static bool transform_state_document(StateDocument *document,
                                     const int *final_provinces,
                                     size_t final_province_count,
                                     const uint8_t *moved_out,
                                     const AssetSet *incoming,
                                     char **output,
                                     char *error, size_t error_size)
{
    EditList edits = {0};
    AssetSet final_assets = {0};
    TextBuffer history_additions = {0}, building_additions = {0};
    const char *newline = newline_of(document->text);
    char history_indent[64], building_indent[64];
    char *province_inner = NULL;
    size_t i;
    bool ok = false;

    if (!copy_asset_set_filtered(&document->assets, moved_out, &final_assets)
        || (incoming && !append_assets(&final_assets, incoming, error, error_size))) {
        if (!error[0]) snprintf(error, error_size, "Mémoire insuffisante pour les assets provinciaux.");
        goto cleanup;
    }
    province_inner = build_id_block_inner(
        document->text,
        &document->tokens.items[document->provinces.open_token],
        &document->tokens.items[document->provinces.close_token],
        final_provinces, final_province_count);
    if (!province_inner
        || !edit_push(&edits,
                      document->tokens.items[document->provinces.open_token].end,
                      document->tokens.items[document->provinces.close_token].start,
                      province_inner)) {
        free(province_inner);
        snprintf(error, error_size, "Mémoire insuffisante pour le bloc provinces.");
        goto cleanup;
    }
    province_inner = NULL;

    line_indent(document->text,
                document->tokens.items[document->history.close_token].start,
                history_indent, sizeof(history_indent));
    if (strlen(history_indent) + 1 < sizeof(history_indent)) strcat(history_indent, "\t");
    if (!build_vp_text(&final_assets, history_indent, newline, &history_additions)) {
        snprintf(error, error_size, "Mémoire insuffisante pour les victory points.");
        goto cleanup;
    }
    for (i = 0; i < document->vp_block_count; ++i) {
        size_t start = document->tokens.items[document->vp_blocks[i].key_token].start;
        size_t end = line_end(document->text,
                              document->tokens.items[document->vp_blocks[i].close_token].end);
        if (!line_prefix_whitespace(document->text, start)) {
            snprintf(error, error_size,
                     "Un bloc victory_points partage sa ligne avec un autre élément; transfert annulé.");
            goto cleanup;
        }
        if (!edit_push(&edits, line_start(document->text, start), end, NULL)) goto memory_error;
    }

    if (document->has_buildings) {
        line_indent(document->text,
                    document->tokens.items[document->buildings.close_token].start,
                    building_indent, sizeof(building_indent));
        if (strlen(building_indent) + 1 < sizeof(building_indent)) strcat(building_indent, "\t");
        for (i = 0; i < document->assets.building_count; ++i) {
            int province = document->assets.buildings[i].province;
            size_t t;
            for (t = document->buildings.open_token + 1;
                 t < document->buildings.close_token; ++t) {
                PtBlock child;
                if (token_integer(document->text, &document->tokens.items[t]) == province
                    && assignment_block_at(document->text, &document->tokens, t, &child)) {
                    size_t start = document->tokens.items[t].start;
                    size_t end = line_end(document->text,
                                          document->tokens.items[child.close_token].end);
                    if (!line_prefix_whitespace(document->text, start)) {
                        snprintf(error, error_size,
                                 "Un bloc de bâtiments provinciaux partage sa ligne; transfert annulé.");
                        goto cleanup;
                    }
                    if (!edit_push(&edits, line_start(document->text, start), end, NULL))
                        goto memory_error;
                    break;
                }
            }
        }
        if (!build_building_text(&final_assets, building_indent, newline, &building_additions))
            goto memory_error;
        if (building_additions.length) {
            size_t position = line_start(document->text,
                document->tokens.items[document->buildings.close_token].start);
            char *insert = building_additions.data;
            building_additions.data = NULL;
            if (!edit_push(&edits, position, position, insert)) {
                free(insert); goto memory_error;
            }
        }
    } else if (final_assets.building_count) {
        TextBuffer block = {0};
        char child_indent[72];
        snprintf(child_indent, sizeof(child_indent), "%s\t", history_indent);
        if (!buffer_append(&block, history_indent)
            || !buffer_append(&block, "buildings = {")
            || !buffer_append(&block, newline)
            || !build_building_text(&final_assets, child_indent, newline, &block)
            || !buffer_append(&block, history_indent)
            || !buffer_append(&block, "}")
            || !buffer_append(&block, newline)) {
            free(block.data); goto memory_error;
        }
        if (!buffer_append_n(&history_additions, block.data, block.length)) {
            free(block.data); goto memory_error;
        }
        free(block.data);
    }
    if (history_additions.length) {
        size_t position = line_end(document->text,
                                   document->tokens.items[document->history.open_token].end);
        char *insert = history_additions.data;
        history_additions.data = NULL;
        if (!edit_push(&edits, position, position, insert)) {
            free(insert); goto memory_error;
        }
    }
    if (!apply_edits(document->text, &edits, output, error, error_size)) goto cleanup;
    if (!province_transfer_validate_syntax(*output, error, error_size)) {
        free(*output); *output = NULL; goto cleanup;
    }
    if (!validate_state_output(*output, final_provinces, final_province_count,
                               &final_assets, error, error_size)) {
        free(*output); *output = NULL; goto cleanup;
    }
    ok = true;
    goto cleanup;

memory_error:
    snprintf(error, error_size, "Mémoire insuffisante pendant la transformation de l'état.");
cleanup:
    edits_free(&edits);
    assets_free(&final_assets);
    free(history_additions.data);
    free(building_additions.data);
    return ok;
}

typedef struct {
    int id;
    const Hoi4State *state;
    int *provinces;
    size_t province_count;
    bool target;
} StatePlan;

typedef struct {
    StatePlan *items;
    size_t count;
    size_t capacity;
} StatePlanList;

static int compare_ints(const void *left, const void *right)
{
    int a = *(const int *)left, b = *(const int *)right;
    return (a > b) - (a < b);
}

static StatePlan *state_plan_add(StatePlanList *plans, int id, const Hoi4State *state,
                                 bool target)
{
    size_t i;
    for (i = 0; i < plans->count; ++i) if (plans->items[i].id == id) return &plans->items[i];
    if (plans->count == plans->capacity) {
        size_t capacity = plans->capacity ? plans->capacity * 2 : 8;
        StatePlan *grown = realloc(plans->items, capacity * sizeof(*grown));
        if (!grown) return NULL;
        plans->items = grown;
        plans->capacity = capacity;
    }
    memset(&plans->items[plans->count], 0, sizeof(plans->items[plans->count]));
    plans->items[plans->count].id = id;
    plans->items[plans->count].state = state;
    plans->items[plans->count].target = target;
    return &plans->items[plans->count++];
}

static void state_plans_free(StatePlanList *plans)
{
    size_t i;
    for (i = 0; i < plans->count; ++i) free(plans->items[i].provinces);
    free(plans->items);
    memset(plans, 0, sizeof(*plans));
}

static char *read_entire_file(const char *path, char *error, size_t error_size)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *content;
    if (!file) {
        snprintf(error, error_size, "Impossible de lire %.600s", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END); size = ftell(file); rewind(file);
    if (size < 0 || !(content = malloc((size_t)size + 1))
        || fread(content, 1, (size_t)size, file) != (size_t)size) {
        free(content); fclose(file);
        snprintf(error, error_size, "Lecture incomplète de %.580s", path);
        return NULL;
    }
    fclose(file);
    content[size] = '\0';
    return content;
}

static bool ensure_transfer_directory(const char *mod_root, const char *first,
                                      const char *second, char *directory, size_t size,
                                      char *error, size_t error_size)
{
    char parent[CP_PATH_MAX];
    if (!cp_path_join(parent, sizeof(parent), mod_root, first)
        || !cp_path_join(directory, size, parent, second)) {
        snprintf(error, error_size, "Chemin du mod trop long.");
        return false;
    }
    if ((!CreateDirectoryA(parent, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        || (!CreateDirectoryA(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)) {
        snprintf(error, error_size, "Impossible de créer %.600s", directory);
        return false;
    }
    return true;
}

static bool target_for_source(const char *directory, const char *source,
                              char *target, size_t size)
{
    const char *filename = strrchr(source, '\\');
    if (!filename) filename = strrchr(source, '/');
    filename = filename ? filename + 1 : source;
    return cp_path_join(target, size, directory, filename);
}

static bool change_add(ChangeList *changes, const char *source, const char *target,
                       char *content, char *error, size_t error_size)
{
    size_t i;
    for (i = 0; i < changes->count; ++i) {
        if (_stricmp(changes->items[i].target, target) == 0) {
            snprintf(error, error_size, "Deux modifications ciblent le même fichier %.500s", target);
            return false;
        }
    }
    if (changes->count == changes->capacity) {
        size_t capacity = changes->capacity ? changes->capacity * 2 : 8;
        FileChange *grown = realloc(changes->items, capacity * sizeof(*grown));
        if (!grown) return false;
        changes->items = grown;
        changes->capacity = capacity;
    }
    memset(&changes->items[changes->count], 0, sizeof(changes->items[changes->count]));
    snprintf(changes->items[changes->count].source, CP_PATH_MAX, "%s", source);
    snprintf(changes->items[changes->count].target, CP_PATH_MAX, "%s", target);
    changes->items[changes->count].content = content;
    changes->count++;
    return true;
}

static void changes_free(ChangeList *changes)
{
    size_t i;
    for (i = 0; i < changes->count; ++i) free(changes->items[i].content);
    free(changes->items);
    memset(changes, 0, sizeof(*changes));
}

static bool write_file_exact(const char *path, const char *content)
{
    FILE *file = fopen(path, "wb");
    size_t length = strlen(content);
    if (!file) return false;
    if (fwrite(content, 1, length, file) != length) {
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

static bool commit_changes(ChangeList *changes, char *error, size_t error_size)
{
    DWORD pid = GetCurrentProcessId();
    ULONGLONG tick = GetTickCount64();
    size_t i, j;
    for (i = 0; i < changes->count; ++i) {
        FileChange *change = &changes->items[i];
        snprintf(change->temp, sizeof(change->temp), "%.4000s.crispy.%lu.%llu.tmp",
                 change->target, (unsigned long)pid, (unsigned long long)tick);
        snprintf(change->backup, sizeof(change->backup), "%.4000s.crispy.%lu.%llu.bak",
                 change->target, (unsigned long)pid, (unsigned long long)tick);
        DeleteFileA(change->temp);
        DeleteFileA(change->backup);
        if (!write_file_exact(change->temp, change->content)) {
            snprintf(error, error_size, "Impossible de préparer %.600s", change->target);
            for (j = 0; j <= i; ++j) DeleteFileA(changes->items[j].temp);
            return false;
        }
    }
    for (i = 0; i < changes->count; ++i) {
        FileChange *change = &changes->items[i];
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
    for (i = 0; i < changes->count; ++i) {
        if (changes->items[i].had_original) DeleteFileA(changes->items[i].backup);
    }
    return true;

rollback:
    for (j = 0; j < changes->count; ++j) {
        FileChange *change = &changes->items[j];
        DeleteFileA(change->temp);
        if (change->committed) {
            DeleteFileA(change->target);
            if (change->had_original)
                MoveFileExA(change->backup, change->target, MOVEFILE_WRITE_THROUGH);
        }
    }
    return false;
}

static int preferred_region(const Hoi4Map *map, const int *provinces, size_t count)
{
    int *counts = calloc(HOI4_MAX_STATES, sizeof(*counts));
    int best = 0, best_count = 0;
    size_t i;
    if (!counts) return 0;
    for (i = 0; i < count; ++i) {
        int province = provinces[i];
        int region = province > 0 && province < HOI4_MAX_PROVINCES
            ? map->provinces[province].strategic_region_id : 0;
        if (region > 0 && region < HOI4_MAX_STATES) {
            counts[region]++;
            if (counts[region] > best_count
                || (counts[region] == best_count && region < best)) {
                best = region;
                best_count = counts[region];
            }
        }
    }
    free(counts);
    return best;
}

bool province_transfer_execute(const Hoi4Map *map,
                               const char *mod_root,
                               const uint8_t *selected_provinces,
                               int target_state_id,
                               ProvinceTransferResult *result)
{
    const Hoi4State *target_state;
    uint8_t *moving = NULL;
    uint8_t *affected_regions = NULL;
    uint16_t *new_region = NULL;
    StatePlanList plans = {0};
    AssetSet incoming = {0};
    ChangeList changes = {0};
    char states_dir[CP_PATH_MAX], regions_dir[CP_PATH_MAX];
    size_t moving_count = 0;
    size_t i, p;
    bool success = false;

    memset(result, 0, sizeof(*result));
    if (!map || !mod_root || !mod_root[0] || !selected_provinces) {
        snprintf(result->error, sizeof(result->error), "Paramètres de transfert invalides.");
        return false;
    }
    target_state = hoi4_map_state(map, target_state_id);
    if (!target_state) {
        snprintf(result->error, sizeof(result->error), "L'état cible %d est introuvable.", target_state_id);
        return false;
    }
    moving = calloc(HOI4_MAX_PROVINCES, 1);
    affected_regions = calloc(HOI4_MAX_STATES, 1);
    new_region = calloc(HOI4_MAX_PROVINCES, sizeof(*new_region));
    if (!moving || !affected_regions || !new_region) {
        snprintf(result->error, sizeof(result->error), "Mémoire insuffisante pour préparer le transfert.");
        goto cleanup;
    }
    for (i = 1; i < HOI4_MAX_PROVINCES; ++i) {
        const Hoi4Province *province;
        const Hoi4State *source;
        if (!selected_provinces[i]) continue;
        province = hoi4_map_province(map, (int)i);
        if (!province || province->type != HOI4_PROVINCE_LAND || !province->state_id) {
            snprintf(result->error, sizeof(result->error),
                     "La province %zu n'appartient à aucun état terrestre.", i);
            goto cleanup;
        }
        if (province->state_id == target_state_id) continue;
        source = hoi4_map_state(map, province->state_id);
        if (!source || !state_plan_add(&plans, source->id, source, false)) {
            snprintf(result->error, sizeof(result->error), "État source invalide pour la province %zu.", i);
            goto cleanup;
        }
        moving[i] = 1;
        moving_count++;
    }
    if (moving_count == 0) {
        snprintf(result->error, sizeof(result->error),
                 "Toutes les provinces sélectionnées sont déjà dans l'état cible.");
        goto cleanup;
    }
    if (!state_plan_add(&plans, target_state_id, target_state, true)) {
        snprintf(result->error, sizeof(result->error), "Mémoire insuffisante pour l'état cible.");
        goto cleanup;
    }
    for (i = 0; i < plans.count; ++i) {
        StatePlan *plan = &plans.items[i];
        size_t capacity = plan->state->province_count + (plan->target ? moving_count : 0);
        plan->provinces = malloc(capacity * sizeof(*plan->provinces));
        if (!plan->provinces) {
            snprintf(result->error, sizeof(result->error), "Mémoire insuffisante pour l'état %d.", plan->id);
            goto cleanup;
        }
        for (p = 0; p < plan->state->province_count; ++p) {
            int province = plan->state->provinces[p];
            if (!moving[province]) plan->provinces[plan->province_count++] = province;
        }
        if (plan->target) {
            for (p = 1; p < HOI4_MAX_PROVINCES; ++p)
                if (moving[p]) plan->provinces[plan->province_count++] = (int)p;
        } else if (plan->province_count == 0) {
            snprintf(result->error, sizeof(result->error),
                     "Le transfert viderait complètement l'état %d; opération annulée.", plan->id);
            goto cleanup;
        }
        qsort(plan->provinces, plan->province_count, sizeof(plan->provinces[0]), compare_ints);
    }
    if (!ensure_transfer_directory(mod_root, "history", "states",
                                   states_dir, sizeof(states_dir),
                                   result->error, sizeof(result->error))
        || !ensure_transfer_directory(mod_root, "map", "strategicregions",
                                      regions_dir, sizeof(regions_dir),
                                      result->error, sizeof(result->error))) goto cleanup;

    /* Source states: collect assets first, then prepare their complete output. */
    for (i = 0; i < plans.count; ++i) {
        StatePlan *plan = &plans.items[i];
        char *input, *output = NULL;
        char target_path[CP_PATH_MAX];
        StateDocument document;
        if (plan->target) continue;
        input = read_entire_file(plan->state->source, result->error, sizeof(result->error));
        if (!input) goto cleanup;
        if (!parse_state_document(input, &document, result->error, sizeof(result->error))) {
            free(input); state_document_free(&document); goto cleanup;
        }
        if (!check_unsupported_nested_assets(&document, moving,
                                             result->error, sizeof(result->error))
            || !collect_moved_assets(&document.assets, moving, &incoming)
            || !transform_state_document(&document, plan->provinces, plan->province_count,
                                         moving, NULL, &output,
                                         result->error, sizeof(result->error))) {
            if (!result->error[0])
                snprintf(result->error, sizeof(result->error), "Échec de transformation de l'état %d.", plan->id);
            state_document_free(&document); free(input); free(output); goto cleanup;
        }
        state_document_free(&document);
        free(input);
        if (!target_for_source(states_dir, plan->state->source,
                               target_path, sizeof(target_path))
            || !change_add(&changes, plan->state->source, target_path, output,
                           result->error, sizeof(result->error))) {
            free(output);
            if (!result->error[0]) snprintf(result->error, sizeof(result->error), "Chemin d'état invalide.");
            goto cleanup;
        }
    }

    /* Target state: keep its assets and append every moved provincial asset. */
    for (i = 0; i < plans.count; ++i) {
        StatePlan *plan = &plans.items[i];
        char *input, *output = NULL;
        char target_path[CP_PATH_MAX];
        StateDocument document;
        if (!plan->target) continue;
        input = read_entire_file(plan->state->source, result->error, sizeof(result->error));
        if (!input) goto cleanup;
        if (!parse_state_document(input, &document, result->error, sizeof(result->error))
            || !transform_state_document(&document, plan->provinces, plan->province_count,
                                         NULL, &incoming, &output,
                                         result->error, sizeof(result->error))) {
            state_document_free(&document); free(input); free(output); goto cleanup;
        }
        state_document_free(&document);
        free(input);
        if (!target_for_source(states_dir, plan->state->source,
                               target_path, sizeof(target_path))
            || !change_add(&changes, plan->state->source, target_path, output,
                           result->error, sizeof(result->error))) {
            free(output); goto cleanup;
        }
        break;
    }

    /* Reassign every province of each affected state to one strategic region. */
    for (p = 1; p < HOI4_MAX_PROVINCES; ++p)
        new_region[p] = map->provinces[p].strategic_region_id;
    for (i = 0; i < plans.count; ++i) {
        StatePlan *plan = &plans.items[i];
        int region = plan->target
            ? preferred_region(map, plan->state->provinces, plan->state->province_count)
            : preferred_region(map, plan->provinces, plan->province_count);
        if (!region) region = preferred_region(map, plan->provinces, plan->province_count);
        if (!region || !hoi4_map_strategic_region(map, region)) {
            snprintf(result->error, sizeof(result->error),
                     "Aucune région stratégique valide pour l'état %d.", plan->id);
            goto cleanup;
        }
        for (p = 0; p < plan->province_count; ++p) {
            int province = plan->provinces[p];
            uint16_t old = new_region[province];
            if (old != region) {
                if (old) affected_regions[old] = 1;
                affected_regions[region] = 1;
                new_region[province] = (uint16_t)region;
            }
        }
    }
    for (i = 1; i < HOI4_MAX_STATES; ++i) {
        const Hoi4StrategicRegion *region;
        int *ids;
        size_t count = 0;
        char *input, *output = NULL;
        char target_path[CP_PATH_MAX];
        if (!affected_regions[i]) continue;
        region = hoi4_map_strategic_region(map, (int)i);
        if (!region) {
            snprintf(result->error, sizeof(result->error), "Région stratégique %zu introuvable.", i);
            goto cleanup;
        }
        ids = malloc(HOI4_MAX_PROVINCES * sizeof(*ids));
        if (!ids) {
            snprintf(result->error, sizeof(result->error), "Mémoire insuffisante pour la région %zu.", i);
            goto cleanup;
        }
        for (p = 1; p < HOI4_MAX_PROVINCES; ++p)
            if (new_region[p] == i) ids[count++] = (int)p;
        qsort(ids, count, sizeof(ids[0]), compare_ints);
        input = read_entire_file(region->source, result->error, sizeof(result->error));
        if (!input || !province_transfer_replace_id_block(input, "provinces",
                                                          ids, count, &output,
                                                          result->error, sizeof(result->error))) {
            free(ids); free(input); free(output); goto cleanup;
        }
        free(ids); free(input);
        if (!target_for_source(regions_dir, region->source,
                               target_path, sizeof(target_path))
            || !change_add(&changes, region->source, target_path, output,
                           result->error, sizeof(result->error))) {
            free(output); goto cleanup;
        }
        result->changed_strategic_regions++;
    }

    if (!commit_changes(&changes, result->error, sizeof(result->error))) goto cleanup;
    result->transferred_provinces = moving_count;
    result->changed_states = plans.count;
    success = true;

cleanup:
    free(moving);
    free(affected_regions);
    free(new_region);
    state_plans_free(&plans);
    assets_free(&incoming);
    changes_free(&changes);
    return success;
}
