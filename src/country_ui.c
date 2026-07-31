#include "country_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNTRY_TOP 64.0f

typedef struct {
    const char *label;
    char *value;
    size_t capacity;
    SDL_FRect rect;
    bool uppercase;
    bool numeric;
} CountryField;

static bool inside(float x, float y, const SDL_FRect *rect)
{
    return x >= rect->x && y >= rect->y
        && x < rect->x + rect->w && y < rect->y + rect->h;
}

static void fill(SDL_Renderer *renderer, SDL_FRect rect,
                 uint8_t r, uint8_t g, uint8_t b)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

static void draw_text(SDL_Renderer *renderer, TTF_Font *font,
                      const char *text, float x, float y, SDL_Color color)
{
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_FRect target;
    if (!font || !text || !text[0]) return;
    surface = TTF_RenderText_Blended(font, text, 0, color);
    if (!surface) return;
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        target = (SDL_FRect){x, y, (float)surface->w, (float)surface->h};
        SDL_RenderTexture(renderer, texture, NULL, &target);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}

static size_t build_fields(CountryCreatorUI *ui, CountryField fields[11])
{
    float left = 32.0f, field_x = 218.0f, y = 94.0f;
    size_t count = 0;
#define FIELD(label_, value_, cap_, upper_, numeric_) do { \
    fields[count++] = (CountryField){label_, value_, cap_, \
        {field_x, y, 470, 36}, upper_, numeric_}; y += 50; \
} while (0)
    FIELD("Tag (3 lettres)", ui->request.tag, sizeof(ui->request.tag), true, false);
    FIELD("Nom du pays", ui->request.name, sizeof(ui->request.name), false, false);
    FIELD("Adjectif", ui->request.adjective, sizeof(ui->request.adjective), false, false);
    FIELD("Nom avec article", ui->request.definite_name,
          sizeof(ui->request.definite_name), false, false);
    FIELD("ID de l’état capital", ui->capital, sizeof(ui->capital), false, true);
#undef FIELD
    (void)left;
    return count;
}

static void sync_numeric(CountryCreatorUI *ui)
{
    int i;
    ui->request.capital = atoi(ui->capital);
    for (i = 0; i < 3; ++i) {
        ui->request.color[i] = atoi(ui->colors[i]);
        ui->request.color_ui[i] = atoi(ui->colors[i + 3]);
    }
}

void country_ui_init(CountryCreatorUI *ui)
{
    int i;
    memset(ui, 0, sizeof(*ui));
    country_create_request_defaults(&ui->request);
    snprintf(ui->capital, sizeof(ui->capital), "%d", ui->request.capital);
    for (i = 0; i < 3; ++i) {
        snprintf(ui->colors[i], sizeof(ui->colors[i]), "%d",
                 ui->request.color[i]);
        snprintf(ui->colors[i + 3], sizeof(ui->colors[i + 3]), "%d",
                 ui->request.color_ui[i]);
    }
    ui->active_field = -1;
    snprintf(ui->status, sizeof(ui->status),
             "Remplis les paramètres du nouveau pays.");
}

void country_ui_reload(CountryCreatorUI *ui)
{
    snprintf(ui->status, sizeof(ui->status),
             "Mod changé. Le formulaire est prêt pour la création.");
}

static void render_field(CountryCreatorUI *ui, SDL_Renderer *renderer,
                         TTF_Font *font_small, const CountryField *field,
                         int index)
{
    SDL_Color white = {235, 239, 247, 255};
    SDL_Color label = {172, 182, 201, 255};
    draw_text(renderer, font_small, field->label, 32,
              field->rect.y + 9, label);
    fill(renderer, field->rect,
         ui->active_field == index ? 49 : 31,
         ui->active_field == index ? 87 : 38,
         ui->active_field == index ? 136 : 52);
    draw_text(renderer, font_small, field->value,
              field->rect.x + 11, field->rect.y + 9, white);
}

void country_ui_render(CountryCreatorUI *ui, SDL_Window *window,
                       SDL_Renderer *renderer,
                       TTF_Font *font, TTF_Font *font_small)
{
    CountryField fields[11];
    size_t field_count = build_fields(ui, fields), i;
    int width, height;
    SDL_Color white = {235, 239, 247, 255};
    SDL_Color muted = {164, 174, 194, 255};
    SDL_Color error = {244, 112, 112, 255};
    SDL_FRect checkbox = {32, 716, 22, 22};
    SDL_FRect create = {520, 704, 168, 42};
    SDL_GetWindowSize(window, &width, &height);
    (void)width; (void)height;
    draw_text(renderer, font, "Nouveau pays", 32, COUNTRY_TOP + 10, white);
    for (i = 0; i < field_count; ++i)
        render_field(ui, renderer, font_small, &fields[i], (int)i);

    draw_text(renderer, font_small, "Culture graphique", 32, 352, muted);
    for (i = 0; i < COUNTRY_CULTURE_COUNT; ++i) {
        float x = 32 + (float)(i % 4) * 166;
        float y = 376 + (float)(i / 4) * 42;
        SDL_FRect button = {x, y, 156, 32};
        fill(renderer, button,
             ui->request.culture == (CountryCulture)i ? 48 : 44,
             ui->request.culture == (CountryCulture)i ? 112 : 51,
             ui->request.culture == (CountryCulture)i ? 196 : 67);
        draw_text(renderer, font_small, country_culture_label((CountryCulture)i),
                  x + 8, y + 8, white);
    }

    draw_text(renderer, font_small, "Parti dirigeant", 32, 464, muted);
    for (i = 0; i < COUNTRY_IDEOLOGY_COUNT; ++i) {
        float x = 32 + (float)i * 166;
        SDL_FRect button = {x, 488, 156, 32};
        fill(renderer, button,
             ui->request.ruling_party == (CountryIdeology)i ? 48 : 44,
             ui->request.ruling_party == (CountryIdeology)i ? 112 : 51,
             ui->request.ruling_party == (CountryIdeology)i ? 196 : 67);
        draw_text(renderer, font_small,
                  country_ideology_name((CountryIdeology)i),
                  x + 12, 496, white);
    }

    draw_text(renderer, font_small, "Couleur principale (R G B)", 32, 548, muted);
    draw_text(renderer, font_small, "Couleur interface (R G B)", 32, 600, muted);
    for (i = 0; i < 6; ++i) {
        float x = 252 + (float)(i % 3) * 76;
        float y = i < 3 ? 538 : 590;
        SDL_FRect box = {x, y, 64, 36};
        int index = 5 + (int)i;
        fill(renderer, box,
             ui->active_field == index ? 49 : 31,
             ui->active_field == index ? 87 : 38,
             ui->active_field == index ? 136 : 52);
        draw_text(renderer, font_small, ui->colors[i], x + 10, y + 9, white);
    }
    {
        SDL_FRect swatch = {490, 538, 92, 88};
        sync_numeric(ui);
        fill(renderer, swatch,
             (uint8_t)SDL_clamp(ui->request.color[0], 0, 255),
             (uint8_t)SDL_clamp(ui->request.color[1], 0, 255),
             (uint8_t)SDL_clamp(ui->request.color[2], 0, 255));
        draw_text(renderer, font_small, "Aperçu", 502, 570,
                  (SDL_Color){255, 255, 255, 255});
    }
    fill(renderer, checkbox, 65, 74, 91);
    if (ui->request.create_placeholder_flags) {
        SDL_FRect mark = {36, 720, 14, 14};
        fill(renderer, mark, 70, 157, 91);
    }
    draw_text(renderer, font_small,
              "Créer des drapeaux placeholders aux trois tailles HOI4",
              66, 718, white);
    fill(renderer, create, 48, 112, 196);
    draw_text(renderer, font_small, "Créer le pays", 552, 716, white);
    draw_text(renderer, font_small, ui->status, 32, 770,
              strstr(ui->status, "Erreur") || strstr(ui->status, "existe")
                  || strstr(ui->status, "Impossible") ? error : muted);
}

static char *active_target(CountryCreatorUI *ui,
                           CountryField *fields, size_t field_count,
                           size_t *capacity, bool *uppercase, bool *numeric)
{
    if (ui->active_field >= 0
        && (size_t)ui->active_field < field_count) {
        CountryField *field = &fields[ui->active_field];
        *capacity = field->capacity;
        *uppercase = field->uppercase;
        *numeric = field->numeric;
        return field->value;
    }
    if (ui->active_field >= 5 && ui->active_field < 11) {
        *capacity = sizeof(ui->colors[0]);
        *uppercase = false;
        *numeric = true;
        return ui->colors[ui->active_field - 5];
    }
    return NULL;
}

static void execute_creation(CountryCreatorUI *ui,
                             const char *game_root, const char *mod_root)
{
    CountryCreateResult result;
    sync_numeric(ui);
    if (!country_creator_execute(&ui->request, game_root, mod_root, &result)) {
        snprintf(ui->status, sizeof(ui->status), "Erreur : %.735s", result.error);
        return;
    }
    snprintf(ui->status, sizeof(ui->status),
             "Pays %s créé avec succès — %zu fichiers écrits.",
             ui->request.tag, result.changed_files);
}

bool country_ui_handle_event(CountryCreatorUI *ui, SDL_Window *window,
                             const SDL_Event *event,
                             const char *game_root, const char *mod_root)
{
    CountryField fields[11];
    size_t field_count = build_fields(ui, fields);
    SDL_FRect checkbox = {32, 716, 22, 22};
    SDL_FRect create = {520, 704, 168, 42};
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && event->button.button == SDL_BUTTON_LEFT) {
        size_t i;
        ui->active_field = -1;
        for (i = 0; i < field_count; ++i) {
            if (inside(event->button.x, event->button.y, &fields[i].rect)) {
                ui->active_field = (int)i;
                break;
            }
        }
        for (i = 0; i < 6; ++i) {
            SDL_FRect box = {
                252 + (float)(i % 3) * 76,
                i < 3 ? 538 : 590, 64, 36
            };
            if (inside(event->button.x, event->button.y, &box))
                ui->active_field = 5 + (int)i;
        }
        for (i = 0; i < COUNTRY_CULTURE_COUNT; ++i) {
            SDL_FRect button = {
                32 + (float)(i % 4) * 166,
                376 + (float)(i / 4) * 42, 156, 32
            };
            if (inside(event->button.x, event->button.y, &button))
                ui->request.culture = (CountryCulture)i;
        }
        for (i = 0; i < COUNTRY_IDEOLOGY_COUNT; ++i) {
            SDL_FRect button = {32 + (float)i * 166, 488, 156, 32};
            if (inside(event->button.x, event->button.y, &button))
                ui->request.ruling_party = (CountryIdeology)i;
        }
        if (inside(event->button.x, event->button.y, &checkbox))
            ui->request.create_placeholder_flags =
                !ui->request.create_placeholder_flags;
        if (inside(event->button.x, event->button.y, &create))
            execute_creation(ui, game_root, mod_root);
        if (ui->active_field >= 0) SDL_StartTextInput(window);
        else SDL_StopTextInput(window);
        return true;
    }
    if (event->type == SDL_EVENT_TEXT_INPUT) {
        size_t capacity = 0, length;
        bool uppercase = false, numeric = false;
        char *target = active_target(ui, fields, field_count,
                                     &capacity, &uppercase, &numeric);
        const unsigned char *p = (const unsigned char *)event->text.text;
        if (!target) return true;
        length = strlen(target);
        while (*p && length + 1 < capacity) {
            unsigned char c = *p++;
            if (numeric && !isdigit(c)) continue;
            if (uppercase && c < 128) c = (unsigned char)toupper(c);
            target[length++] = (char)c;
        }
        target[length] = '\0';
        if (ui->active_field == 0 && strlen(target) > 3) target[3] = '\0';
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        size_t capacity = 0;
        bool uppercase = false, numeric = false;
        char *target = active_target(ui, fields, field_count,
                                     &capacity, &uppercase, &numeric);
        bool ctrl = (event->key.mod & SDL_KMOD_CTRL) != 0;
        if (ctrl && (event->key.key == SDLK_RETURN
                     || event->key.key == SDLK_KP_ENTER)) {
            execute_creation(ui, game_root, mod_root);
        } else if (event->key.key == SDLK_TAB) {
            ui->active_field = (ui->active_field + 1) % 11;
            SDL_StartTextInput(window);
        } else if (event->key.key == SDLK_BACKSPACE && target) {
            size_t length = strlen(target);
            if (length) target[length - 1] = '\0';
        } else if (ctrl && event->key.key == SDLK_C && target) {
            SDL_SetClipboardText(target);
        } else if (ctrl && event->key.key == SDLK_V && target) {
            char *clip = SDL_GetClipboardText();
            const unsigned char *p = (const unsigned char *)(clip ? clip : "");
            size_t length = strlen(target);
            while (*p && length + 1 < capacity) {
                unsigned char c = *p++;
                if (numeric && !isdigit(c)) continue;
                if (uppercase && c < 128) c = (unsigned char)toupper(c);
                target[length++] = (char)c;
            }
            target[length] = '\0';
            if (ui->active_field == 0 && strlen(target) > 3) target[3] = '\0';
            SDL_free(clip);
        }
        return true;
    }
    return false;
}
