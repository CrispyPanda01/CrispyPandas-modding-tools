#include "character_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "path_utils.h"
#include "portrait_image.h"

#define CHARACTER_TOP 64.0f
#define FORM_LEFT 24.0f
#define FORM_WIDTH 750.0f
#define TRAIT_LEFT 800.0f

typedef struct {
    const char *label;
    char *value;
    size_t capacity;
    float y;
    bool uppercase;
} UiTextField;

typedef struct {
    const char *label;
    int *value;
    float x;
    float y;
} UiStepper;

typedef struct {
    UiTextField fields[16];
    size_t field_count;
    UiStepper steppers[20];
    size_t stepper_count;
    float roles_y;
    float portrait_y;
    float end_y;
} CharacterLayout;

static bool inside(float x, float y, float rx, float ry, float rw, float rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
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

static void fill(SDL_Renderer *renderer, float x, float y, float w, float h,
                 uint8_t r, uint8_t g, uint8_t b)
{
    SDL_FRect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

static void add_field(CharacterLayout *layout, const char *label,
                      char *value, size_t capacity, float y, bool uppercase)
{
    if (layout->field_count >= sizeof(layout->fields) / sizeof(layout->fields[0]))
        return;
    layout->fields[layout->field_count++] =
        (UiTextField){label, value, capacity, y, uppercase};
}

static void add_stepper(CharacterLayout *layout, const char *label,
                        int *value, float x, float y)
{
    if (layout->stepper_count
        >= sizeof(layout->steppers) / sizeof(layout->steppers[0])) return;
    layout->steppers[layout->stepper_count++] =
        (UiStepper){label, value, x, y};
}

static void build_layout(CharacterCreatorUI *ui, CharacterLayout *layout)
{
    float y = 86.0f - ui->form_scroll;
    memset(layout, 0, sizeof(*layout));
    add_field(layout, "Tag du pays", ui->request.country_tag,
              sizeof(ui->request.country_tag), y, true);
    y += 46;
    add_field(layout, "Nom affiché", ui->request.name,
              sizeof(ui->request.name), y, false);
    y += 46;
    add_field(layout, "Token", ui->request.token,
              sizeof(ui->request.token), y, false);
    y += 58;
    layout->roles_y = y;
    y += 84;
    if (ui->request.roles & CHARACTER_ROLE_COUNTRY_LEADER) {
        y += 30;
        add_field(layout, "Idéologie", ui->request.ideology,
                  sizeof(ui->request.ideology), y, false);
        y += 46;
        add_field(layout, "Expiration", ui->request.expire,
                  sizeof(ui->request.expire), y, false);
        y += 54;
    }
    if (ui->request.roles & CHARACTER_ROLE_ADVISOR) {
        y += 30;
        add_field(layout, "Slot advisor", ui->request.advisor_slot,
                  sizeof(ui->request.advisor_slot), y, false);
        y += 46;
        add_field(layout, "Ledger (optionnel)", ui->request.advisor_ledger,
                  sizeof(ui->request.advisor_ledger), y, false);
        y += 48;
        add_stepper(layout, "Coût", &ui->request.advisor_cost, 162, y);
        y += 54;
    }
    if (ui->request.roles
        & (CHARACTER_ROLE_GENERAL | CHARACTER_ROLE_FIELD_MARSHAL)) {
        y += 34;
        add_stepper(layout, "Skill", &ui->request.land_skill, 120, y);
        add_stepper(layout, "Attaque", &ui->request.attack_skill, 245, y);
        add_stepper(layout, "Défense", &ui->request.defense_skill, 370, y);
        add_stepper(layout, "Planning", &ui->request.planning_skill, 495, y);
        add_stepper(layout, "Logistique", &ui->request.logistics_skill, 620, y);
        y += 62;
    }
    if (ui->request.roles & CHARACTER_ROLE_NAVY_LEADER) {
        y += 34;
        add_stepper(layout, "Skill", &ui->request.navy_skill, 120, y);
        add_stepper(layout, "Attaque", &ui->request.navy_attack_skill, 245, y);
        add_stepper(layout, "Défense", &ui->request.navy_defense_skill, 370, y);
        add_stepper(layout, "Manœuvre", &ui->request.maneuvering_skill, 495, y);
        add_stepper(layout, "Coord.", &ui->request.coordination_skill, 620, y);
        y += 62;
    }
    if (ui->request.roles & CHARACTER_ROLE_SCIENTIST) {
        y += 30;
        add_field(layout, "Spécialisation", ui->request.scientist_specialization,
                  sizeof(ui->request.scientist_specialization), y, false);
        y += 48;
        add_stepper(layout, "Niveau", &ui->request.scientist_skill, 162, y);
        y += 54;
    }
    layout->portrait_y = y + 32;
    layout->end_y = layout->portrait_y + 178;
    ui->content_height = layout->end_y + ui->form_scroll;
}

static void slug_from_name(CharacterCreatorUI *ui)
{
    char slug[100];
    size_t n = 0;
    const unsigned char *cursor = (const unsigned char *)ui->request.name;
    if (ui->token_manual || strlen(ui->request.country_tag) != 3) return;
    while (*cursor && n + 1 < sizeof(slug)) {
        unsigned char c = *cursor++;
        if (isalnum(c)) slug[n++] = (char)tolower(c);
        else if (n && slug[n - 1] != '_') slug[n++] = '_';
    }
    while (n && slug[n - 1] == '_') n--;
    slug[n] = '\0';
    snprintf(ui->request.token, sizeof(ui->request.token), "%s_%s",
             ui->request.country_tag, slug);
}

static char *traits_for_role(CharacterCreatorUI *ui, uint32_t role)
{
    if (role == CHARACTER_ROLE_COUNTRY_LEADER) return ui->request.country_traits;
    if (role == CHARACTER_ROLE_ADVISOR) return ui->request.advisor_traits;
    if (role == CHARACTER_ROLE_NAVY_LEADER) return ui->request.navy_traits;
    if (role == CHARACTER_ROLE_SCIENTIST) return ui->request.scientist_traits;
    return ui->request.land_traits;
}

static size_t traits_capacity_for_role(uint32_t role)
{
    (void)role;
    return 2048;
}

static uint32_t normalized_trait_role(int role)
{
    if (role == CHARACTER_ROLE_FIELD_MARSHAL)
        return CHARACTER_ROLE_GENERAL;
    return (uint32_t)role;
}

static bool contains_case_insensitive(const char *text, const char *needle)
{
    size_t length = strlen(needle);
    if (!length) return true;
    while (*text) {
        if (_strnicmp(text, needle, length) == 0) return true;
        text++;
    }
    return false;
}

static bool trait_matches(const CharacterTrait *trait, uint32_t role,
                          const char *search)
{
    return (trait->roles & role)
        && contains_case_insensitive(trait->token, search);
}

static int filtered_trait_at(CharacterCreatorUI *ui, int filtered_index)
{
    size_t i;
    uint32_t role = normalized_trait_role(ui->trait_role);
    int current = 0;
    for (i = 0; i < ui->traits.count; ++i) {
        if (!trait_matches(&ui->traits.items[i], role, ui->trait_search)) continue;
        if (current++ == filtered_index) return (int)i;
    }
    return -1;
}

static int filtered_trait_count(CharacterCreatorUI *ui)
{
    size_t i;
    uint32_t role = normalized_trait_role(ui->trait_role);
    int count = 0;
    for (i = 0; i < ui->traits.count; ++i)
        if (trait_matches(&ui->traits.items[i], role, ui->trait_search)) count++;
    return count;
}

static SDL_Texture *texture_from_image(SDL_Renderer *renderer,
                                       const PortraitImage *image)
{
    SDL_Surface *surface;
    SDL_Texture *texture;
    surface = SDL_CreateSurfaceFrom(image->width, image->height,
                                    SDL_PIXELFORMAT_RGBA32,
                                    image->pixels, image->width * 4);
    if (!surface) return NULL;
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    return texture;
}

static bool update_preview(CharacterCreatorUI *ui, SDL_Renderer *renderer,
                           bool large, const char *path)
{
    PortraitImage source = {0}, resized = {0};
    SDL_Texture **target = large ? &ui->large_preview : &ui->small_preview;
    SDL_Texture *replacement = NULL;
    char error[512] = "";
    int width = large ? 156 : 65;
    int height = large ? 210 : 67;
    if (!portrait_image_load(path, &source, error, sizeof(error))
        || !portrait_image_resize_cover(&source, width, height, &resized,
                                        error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "%s", error);
        portrait_image_free(&source);
        portrait_image_free(&resized);
        return false;
    } else {
        replacement = texture_from_image(renderer, &resized);
        if (!replacement) {
            snprintf(ui->status, sizeof(ui->status),
                     "Impossible de créer l'aperçu du portrait : %s",
                     SDL_GetError());
        } else {
            if (*target) SDL_DestroyTexture(*target);
            *target = replacement;
            if (large && !ui->request.small_portrait[0]) {
                PortraitImage automatic_small = {0};
                if (portrait_image_compose_advisor_small(
                        &source, &automatic_small, error, sizeof(error))) {
                    SDL_Texture *small_texture =
                        texture_from_image(renderer, &automatic_small);
                    if (small_texture) {
                        if (ui->small_preview)
                            SDL_DestroyTexture(ui->small_preview);
                        ui->small_preview = small_texture;
                        snprintf(ui->status, sizeof(ui->status),
                                 "Large chargé; small advisor cadré et incliné automatiquement.");
                    }
                }
                portrait_image_free(&automatic_small);
            } else {
                snprintf(ui->status, sizeof(ui->status),
                         "%s portrait chargé (%dx%d → %dx%d).",
                         large ? "Large" : "Small",
                         source.width, source.height, width, height);
            }
        }
    }
    portrait_image_free(&source);
    portrait_image_free(&resized);
    return replacement != NULL;
}

void character_ui_init(CharacterCreatorUI *ui,
                       const char *game_root, const char *mod_root)
{
    memset(ui, 0, sizeof(*ui));
    character_create_request_defaults(&ui->request);
    ui->trait_role = CHARACTER_ROLE_COUNTRY_LEADER;
    character_ui_reload(ui, game_root, mod_root);
}

void character_ui_reload(CharacterCreatorUI *ui,
                         const char *game_root, const char *mod_root)
{
    if (character_trait_catalog_load(&ui->traits, game_root, mod_root))
        snprintf(ui->status, sizeof(ui->status),
                 "%zu traits chargés depuis le jeu et le mod.", ui->traits.count);
    else
        snprintf(ui->status, sizeof(ui->status), "%.700s", ui->traits.error);
}

void character_ui_free(CharacterCreatorUI *ui)
{
    if (ui->large_preview) SDL_DestroyTexture(ui->large_preview);
    if (ui->small_preview) SDL_DestroyTexture(ui->small_preview);
    character_trait_catalog_free(&ui->traits);
    memset(ui, 0, sizeof(*ui));
}

static void render_field(CharacterCreatorUI *ui, SDL_Renderer *renderer,
                         TTF_Font *font, TTF_Font *small,
                         const UiTextField *field, int index)
{
    SDL_Color white = {235, 239, 247, 255};
    SDL_Color muted = {151, 162, 184, 255};
    float box_x = 178;
    draw_text(renderer, small, field->label, FORM_LEFT, field->y + 10, muted);
    fill(renderer, box_x, field->y, 570, 36,
         ui->active_field == index ? 54 : 35,
         ui->active_field == index ? 102 : 42,
         ui->active_field == index ? 158 : 56);
    fill(renderer, box_x + 2, field->y + 2, 566, 32, 19, 24, 34);
    draw_text(renderer, font, field->value[0] ? field->value : "—",
              box_x + 9, field->y + 7,
              field->value[0] ? white : muted);
}

static void render_stepper(SDL_Renderer *renderer, TTF_Font *small,
                           const UiStepper *stepper)
{
    char value[24];
    SDL_Color white = {235, 239, 247, 255};
    draw_text(renderer, small, stepper->label, stepper->x,
              stepper->y - 20, (SDL_Color){151, 162, 184, 255});
    fill(renderer, stepper->x, stepper->y, 28, 30, 47, 56, 73);
    fill(renderer, stepper->x + 66, stepper->y, 28, 30, 47, 56, 73);
    draw_text(renderer, small, "−", stepper->x + 9, stepper->y + 6, white);
    draw_text(renderer, small, "+", stepper->x + 75, stepper->y + 6, white);
    snprintf(value, sizeof(value), "%d", *stepper->value);
    draw_text(renderer, small, value, stepper->x + 42, stepper->y + 6, white);
}

static void render_role(SDL_Renderer *renderer, TTF_Font *small,
                        float x, float y, float width,
                        const char *label, bool selected)
{
    fill(renderer, x, y, width, 30,
         selected ? 47 : 38, selected ? 112 : 47, selected ? 190 : 62);
    draw_text(renderer, small, label, x + 9, y + 7,
              (SDL_Color){235, 239, 247, 255});
}

static void render_trait_panel(CharacterCreatorUI *ui,
                               SDL_Window *window, SDL_Renderer *renderer,
                               TTF_Font *font, TTF_Font *small)
{
    int window_width, window_height;
    static const struct { uint32_t role; const char *label; } tabs[] = {
        {CHARACTER_ROLE_COUNTRY_LEADER, "Leader"},
        {CHARACTER_ROLE_ADVISOR, "Advisor"},
        {CHARACTER_ROLE_GENERAL, "Terre"},
        {CHARACTER_ROLE_NAVY_LEADER, "Marine"},
        {CHARACTER_ROLE_SCIENTIST, "Science"}
    };
    size_t i;
    int first = (int)(ui->trait_scroll / 30.0f);
    int rows;
    char summary[128];
    SDL_Color white = {235, 239, 247, 255};
    SDL_Color muted = {151, 162, 184, 255};
    (void)window;
    SDL_GetRenderOutputSize(renderer, &window_width, &window_height);
    fill(renderer, TRAIT_LEFT, 76, window_width - TRAIT_LEFT - 18,
         window_height - 92, 20, 25, 35);
    draw_text(renderer, font, "Traits disponibles", TRAIT_LEFT + 18, 90, white);
    fill(renderer, window_width - 190.0f, 86, 154, 30, 91, 48, 54);
    draw_text(renderer, small, "Tout désélectionner",
              window_width - 178.0f, 93, white);
    for (i = 0; i < sizeof(tabs) / sizeof(tabs[0]); ++i)
        render_role(renderer, small, TRAIT_LEFT + 18 + (float)i * 92, 124, 84,
                    tabs[i].label, ui->trait_role == (int)tabs[i].role);
    fill(renderer, TRAIT_LEFT + 18, 164, window_width - TRAIT_LEFT - 54, 34,
         ui->active_field == -2 ? 52 : 35,
         ui->active_field == -2 ? 101 : 42,
         ui->active_field == -2 ? 158 : 56);
    draw_text(renderer, small,
              ui->trait_search[0] ? ui->trait_search : "Rechercher un trait...",
              TRAIT_LEFT + 28, 173,
              ui->trait_search[0] ? white : muted);
    rows = (window_height - 250) / 30;
    for (i = 0; i < (size_t)rows; ++i) {
        int catalog_index = filtered_trait_at(ui, first + (int)i);
        const CharacterTrait *trait;
        bool selected;
        float y;
        if (catalog_index < 0) break;
        trait = &ui->traits.items[catalog_index];
        selected = character_trait_list_contains(
            traits_for_role(ui, normalized_trait_role(ui->trait_role)),
            trait->token);
        y = 211 + (float)i * 30;
        fill(renderer, TRAIT_LEFT + 18, y, 22, 22,
             selected ? 52 : 48, selected ? 148 : 57, selected ? 83 : 72);
        if (selected) draw_text(renderer, small, "✓", TRAIT_LEFT + 23, y + 2, white);
        draw_text(renderer, small, trait->token, TRAIT_LEFT + 50, y + 3, white);
    }
    snprintf(summary, sizeof(summary), "%d résultat(s) — cliquer pour ajouter/retirer",
             filtered_trait_count(ui));
    draw_text(renderer, small, summary, TRAIT_LEFT + 18,
              (float)window_height - 35, muted);
}

void character_ui_render(CharacterCreatorUI *ui,
                         SDL_Window *window, SDL_Renderer *renderer,
                         TTF_Font *font, TTF_Font *font_small)
{
    CharacterLayout layout;
    int window_width, window_height;
    size_t i;
    SDL_Rect clip;
    SDL_Color white = {235, 239, 247, 255};
    SDL_Color muted = {151, 162, 184, 255};
    build_layout(ui, &layout);
    SDL_GetWindowSize(window, &window_width, &window_height);
    fill(renderer, 0, CHARACTER_TOP, (float)window_width,
         (float)window_height - CHARACTER_TOP, 10, 14, 21);
    clip = (SDL_Rect){0, (int)CHARACTER_TOP,
                      (int)(FORM_LEFT + FORM_WIDTH),
                      window_height - (int)CHARACTER_TOP - 58};
    SDL_SetRenderClipRect(renderer, &clip);
    draw_text(renderer, font, "Nouveau character", FORM_LEFT, 69 - ui->form_scroll, white);
    for (i = 0; i < layout.field_count; ++i)
        render_field(ui, renderer, font, font_small, &layout.fields[i], (int)i);
    draw_text(renderer, font_small, "Rôles (sélection multiple)",
              FORM_LEFT, layout.roles_y - 25, muted);
    render_role(renderer, font_small, 24, layout.roles_y, 112, "Leader pays",
                (ui->request.roles & CHARACTER_ROLE_COUNTRY_LEADER) != 0);
    render_role(renderer, font_small, 144, layout.roles_y, 100, "Advisor",
                (ui->request.roles & CHARACTER_ROLE_ADVISOR) != 0);
    render_role(renderer, font_small, 252, layout.roles_y, 100, "Général",
                (ui->request.roles & CHARACTER_ROLE_GENERAL) != 0);
    render_role(renderer, font_small, 360, layout.roles_y, 100, "Maréchal",
                (ui->request.roles & CHARACTER_ROLE_FIELD_MARSHAL) != 0);
    render_role(renderer, font_small, 468, layout.roles_y, 100, "Amiral",
                (ui->request.roles & CHARACTER_ROLE_NAVY_LEADER) != 0);
    render_role(renderer, font_small, 576, layout.roles_y, 100, "Scientist",
                (ui->request.roles & CHARACTER_ROLE_SCIENTIST) != 0);
    for (i = 0; i < layout.stepper_count; ++i)
        render_stepper(renderer, font_small, &layout.steppers[i]);

    draw_text(renderer, font, "Portraits", FORM_LEFT, layout.portrait_y - 30, white);
    fill(renderer, 24, layout.portrait_y, 330, 42,
         ui->portrait_drop_target == 1 ? 49 : 42,
         ui->portrait_drop_target == 1 ? 132 : 76,
         ui->portrait_drop_target == 1 ? 79 : 119);
    fill(renderer, 370, layout.portrait_y, 330, 42,
         ui->portrait_drop_target == 2 ? 49 : 42,
         ui->portrait_drop_target == 2 ? 132 : 76,
         ui->portrait_drop_target == 2 ? 79 : 119);
    draw_text(renderer, font_small, "Choisir ou déposer le large (156×210)",
              42, layout.portrait_y + 12, white);
    draw_text(renderer, font_small, "Choisir ou déposer le small (65×67)",
              388, layout.portrait_y + 12, white);
    if (ui->large_preview) {
        SDL_FRect target = {24, layout.portrait_y + 52, 78, 105};
        SDL_RenderTexture(renderer, ui->large_preview, NULL, &target);
    }
    if (ui->small_preview) {
        SDL_FRect target = {370, layout.portrait_y + 52, 65, 67};
        SDL_RenderTexture(renderer, ui->small_preview, NULL, &target);
    }
    draw_text(renderer, font_small,
              ui->request.large_portrait[0] ? ui->request.large_portrait : "Aucun large",
              112, layout.portrait_y + 62, muted);
    draw_text(renderer, font_small,
              ui->request.small_portrait[0] ? ui->request.small_portrait
                                           : "Composé et incliné automatiquement depuis le large",
              445, layout.portrait_y + 62, muted);
    SDL_SetRenderClipRect(renderer, NULL);

    render_trait_panel(ui, window, renderer, font, font_small);
    fill(renderer, 0, window_height - 54.0f, TRAIT_LEFT - 10, 54, 23, 29, 40);
    draw_text(renderer, font_small, ui->status, 24, window_height - 35.0f,
              ui->status[0] ? muted : white);
    fill(renderer, 618, window_height - 45.0f, 150, 36, 42, 123, 72);
    draw_text(renderer, font_small, "Créer le character", 635,
              window_height - 35.0f, white);
}

static void activate_field(CharacterCreatorUI *ui, SDL_Window *window, int field)
{
    ui->active_field = field;
    SDL_StartTextInput(window);
    if (field == 2) ui->token_manual = true;
}

static void append_text(CharacterCreatorUI *ui, const CharacterLayout *layout,
                        const char *text)
{
    char *target;
    size_t capacity;
    bool uppercase = false;
    size_t length;
    if (ui->active_field == -2) {
        target = ui->trait_search;
        capacity = sizeof(ui->trait_search);
    } else if (ui->active_field >= 0
               && (size_t)ui->active_field < layout->field_count) {
        const UiTextField *field = &layout->fields[ui->active_field];
        target = field->value;
        capacity = field->capacity;
        uppercase = field->uppercase;
    } else return;
    length = strlen(target);
    while (*text && length + 1 < capacity) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 32) target[length++] = uppercase ? (char)toupper(c) : (char)c;
    }
    target[length] = '\0';
    if (ui->active_field == 0 && strlen(ui->request.country_tag) > 3)
        ui->request.country_tag[3] = '\0';
    if (ui->active_field == 0 || ui->active_field == 1) slug_from_name(ui);
    ui->trait_scroll = 0;
}

static void backspace_field(CharacterCreatorUI *ui, const CharacterLayout *layout)
{
    char *target = NULL;
    size_t length;
    if (ui->active_field == -2) target = ui->trait_search;
    else if (ui->active_field >= 0
             && (size_t)ui->active_field < layout->field_count)
        target = layout->fields[ui->active_field].value;
    if (target && target[0]) {
        length = strlen(target);
        length--;
        while (length > 0
               && (((unsigned char)target[length] & 0xc0) == 0x80))
            length--;
        target[length] = '\0';
    }
    if (ui->active_field == 0 || ui->active_field == 1) slug_from_name(ui);
    ui->trait_scroll = 0;
}

static char *active_text_target(CharacterCreatorUI *ui,
                                const CharacterLayout *layout)
{
    if (ui->active_field == -2) return ui->trait_search;
    if (ui->active_field >= 0
        && (size_t)ui->active_field < layout->field_count)
        return layout->fields[ui->active_field].value;
    return NULL;
}

static void copy_active_field(CharacterCreatorUI *ui,
                              const CharacterLayout *layout)
{
    char *target = active_text_target(ui, layout);
    if (!target) return;
    if (SDL_SetClipboardText(target))
        snprintf(ui->status, sizeof(ui->status),
                 "Texte copié dans le presse-papiers.");
    else
        snprintf(ui->status, sizeof(ui->status),
                 "Impossible de copier : %s", SDL_GetError());
}

static void paste_active_field(CharacterCreatorUI *ui,
                               const CharacterLayout *layout)
{
    char *clipboard;
    if (!active_text_target(ui, layout)) return;
    clipboard = SDL_GetClipboardText();
    if (!clipboard) {
        snprintf(ui->status, sizeof(ui->status),
                 "Impossible de coller : %s", SDL_GetError());
        return;
    }
    append_text(ui, layout, clipboard);
    SDL_free(clipboard);
}

static void choose_portrait(CharacterCreatorUI *ui, SDL_Renderer *renderer,
                            bool large)
{
    char selected[CP_PATH_MAX];
    if (!cp_choose_image_file(large ? "Choisir le portrait large"
                                    : "Choisir le portrait small",
                              selected, sizeof(selected))) return;
    if (update_preview(ui, renderer, large, selected))
        snprintf(large ? ui->request.large_portrait : ui->request.small_portrait,
                 CP_PATH_MAX, "%s", selected);
}

static int portrait_target_at(CharacterCreatorUI *ui, float x, float y)
{
    CharacterLayout layout;
    build_layout(ui, &layout);
    if (inside(x, y, 24, layout.portrait_y, 330, 164)) return 1;
    if (inside(x, y, 370, layout.portrait_y, 330, 164)) return 2;
    return 0;
}

static void accept_dropped_portrait(CharacterCreatorUI *ui,
                                    SDL_Renderer *renderer,
                                    int target, const char *path)
{
    bool large = target == 1;
    if (!path || !path[0] || (target != 1 && target != 2)) {
        snprintf(ui->status, sizeof(ui->status),
                 "Déposez l'image directement sur la zone Large ou Small.");
        return;
    }
    if (update_preview(ui, renderer, large, path))
        snprintf(large ? ui->request.large_portrait : ui->request.small_portrait,
                 CP_PATH_MAX, "%s", path);
}

static void execute_creation(CharacterCreatorUI *ui,
                             const char *game_root, const char *mod_root,
                             SDL_Window *window)
{
    CharacterCreateResult result;
    if (!character_create_execute(game_root, mod_root, &ui->request, &result)) {
        snprintf(ui->status, sizeof(ui->status), "Erreur : %.700s", result.error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Création annulée", result.error, window);
        return;
    }
    snprintf(ui->status, sizeof(ui->status),
             "%s créé — %zu fichiers écrits. Définition : %.420s",
             ui->request.token, result.changed_files, result.character_file);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
                             "Character créé", ui->status, window);
    ui->token_manual = false;
    ui->request.name[0] = '\0';
    ui->request.token[0] = '\0';
}

bool character_ui_handle_event(CharacterCreatorUI *ui,
                               SDL_Window *window, SDL_Renderer *renderer,
                               const SDL_Event *event,
                               const char *game_root, const char *mod_root)
{
    CharacterLayout layout;
    int window_width, window_height;
    size_t i;
    build_layout(ui, &layout);
    SDL_GetWindowSize(window, &window_width, &window_height);
    if (event->type == SDL_EVENT_DROP_POSITION) {
        ui->portrait_drop_target =
            portrait_target_at(ui, event->drop.x, event->drop.y);
        return true;
    }
    if (event->type == SDL_EVENT_DROP_FILE) {
        int target = portrait_target_at(ui, event->drop.x, event->drop.y);
        if (!target) target = ui->portrait_drop_target;
        accept_dropped_portrait(ui, renderer, target, event->drop.data);
        ui->portrait_drop_target = 0;
        return true;
    }
    if (event->type == SDL_EVENT_DROP_COMPLETE
        || event->type == SDL_EVENT_DROP_BEGIN) {
        ui->portrait_drop_target = 0;
        return true;
    }
    if (event->type == SDL_EVENT_TEXT_INPUT) {
        append_text(ui, &layout, event->text.text);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if ((event->key.mod & SDL_KMOD_CTRL) && event->key.key == SDLK_C)
            copy_active_field(ui, &layout);
        else if ((event->key.mod & SDL_KMOD_CTRL) && event->key.key == SDLK_V)
            paste_active_field(ui, &layout);
        else if (event->key.key == SDLK_BACKSPACE) backspace_field(ui, &layout);
        else if (event->key.key == SDLK_ESCAPE) {
            ui->active_field = -1;
            SDL_StopTextInput(window);
        } else if ((event->key.mod & SDL_KMOD_CTRL)
                   && (event->key.key == SDLK_RETURN
                       || event->key.key == SDLK_KP_ENTER))
            execute_creation(ui, game_root, mod_root, window);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        if (mx < TRAIT_LEFT) {
            float maximum = ui->content_height > window_height - 70
                ? ui->content_height - (window_height - 70) : 0;
            ui->form_scroll -= event->wheel.y * 48;
            if (ui->form_scroll < 0) ui->form_scroll = 0;
            if (ui->form_scroll > maximum) ui->form_scroll = maximum;
        } else {
            int count = filtered_trait_count(ui);
            float maximum = count * 30.0f - (window_height - 250);
            if (maximum < 0) maximum = 0;
            ui->trait_scroll -= event->wheel.y * 60;
            if (ui->trait_scroll < 0) ui->trait_scroll = 0;
            if (ui->trait_scroll > maximum) ui->trait_scroll = maximum;
        }
        return true;
    }
    if (event->type != SDL_EVENT_MOUSE_BUTTON_DOWN
        || event->button.button != SDL_BUTTON_LEFT) return false;
    if (event->button.y >= window_height - 54
        && inside(event->button.x, event->button.y,
                  618, window_height - 45.0f, 150, 36)) {
        execute_creation(ui, game_root, mod_root, window);
        return true;
    }
    if (inside(event->button.x, event->button.y,
               window_width - 190.0f, 86, 154, 30)) {
        ui->request.country_traits[0] = '\0';
        ui->request.advisor_traits[0] = '\0';
        ui->request.land_traits[0] = '\0';
        ui->request.navy_traits[0] = '\0';
        ui->request.scientist_traits[0] = '\0';
        snprintf(ui->status, sizeof(ui->status),
                 "Tous les traits sélectionnés ont été retirés.");
        return true;
    }
    for (i = 0; i < layout.field_count; ++i) {
        if (inside(event->button.x, event->button.y,
                   178, layout.fields[i].y, 570, 36)) {
            activate_field(ui, window, (int)i);
            return true;
        }
    }
    {
        static const uint32_t roles[] = {
            CHARACTER_ROLE_COUNTRY_LEADER, CHARACTER_ROLE_ADVISOR,
            CHARACTER_ROLE_GENERAL, CHARACTER_ROLE_FIELD_MARSHAL,
            CHARACTER_ROLE_NAVY_LEADER, CHARACTER_ROLE_SCIENTIST
        };
        static const float role_x[] = {24, 144, 252, 360, 468, 576};
        static const float role_width[] = {112, 100, 100, 100, 100, 100};
        for (i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i) {
            if (inside(event->button.x, event->button.y,
                       role_x[i], layout.roles_y, role_width[i], 30)) {
                ui->request.roles ^= roles[i];
                return true;
            }
        }
    }
    for (i = 0; i < layout.stepper_count; ++i) {
        UiStepper *stepper = &layout.steppers[i];
        if (inside(event->button.x, event->button.y,
                   stepper->x, stepper->y, 28, 30)) {
            if (*stepper->value > 0) (*stepper->value)--;
            return true;
        }
        if (inside(event->button.x, event->button.y,
                   stepper->x + 66, stepper->y, 28, 30)) {
            if (*stepper->value < 10000) (*stepper->value)++;
            return true;
        }
    }
    if (inside(event->button.x, event->button.y,
               24, layout.portrait_y, 330, 42)) {
        choose_portrait(ui, renderer, true);
        return true;
    }
    if (inside(event->button.x, event->button.y,
               370, layout.portrait_y, 330, 42)) {
        choose_portrait(ui, renderer, false);
        return true;
    }
    {
        static const uint32_t tabs[] = {
            CHARACTER_ROLE_COUNTRY_LEADER, CHARACTER_ROLE_ADVISOR,
            CHARACTER_ROLE_GENERAL, CHARACTER_ROLE_NAVY_LEADER,
            CHARACTER_ROLE_SCIENTIST
        };
        for (i = 0; i < sizeof(tabs) / sizeof(tabs[0]); ++i) {
            if (inside(event->button.x, event->button.y,
                       TRAIT_LEFT + 18 + (float)i * 92, 124, 84, 30)) {
                ui->trait_role = (int)tabs[i];
                ui->trait_scroll = 0;
                return true;
            }
        }
    }
    if (inside(event->button.x, event->button.y,
               TRAIT_LEFT + 18, 164, window_width - TRAIT_LEFT - 54, 34)) {
        activate_field(ui, window, -2);
        return true;
    }
    if (event->button.x >= TRAIT_LEFT + 18 && event->button.y >= 211) {
        int row = (int)((event->button.y - 211) / 30);
        int filtered = (int)(ui->trait_scroll / 30.0f) + row;
        int catalog_index = filtered_trait_at(ui, filtered);
        if (catalog_index >= 0) {
            char *list = traits_for_role(ui, normalized_trait_role(ui->trait_role));
            if (!character_trait_list_toggle(
                    list, traits_capacity_for_role((uint32_t)ui->trait_role),
                    ui->traits.items[catalog_index].token))
                snprintf(ui->status, sizeof(ui->status),
                         "La liste de traits est pleine.");
            return true;
        }
    }
    ui->active_field = -1;
    SDL_StopTextInput(window);
    return true;
}
