#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <windows.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hoi4_map.h"
#include "path_utils.h"
#include "province_transfer.h"
#include "state_edit.h"

#define WINDOW_WIDTH 1440
#define WINDOW_HEIGHT 850
#define TOOLBAR_HEIGHT 64.0f

typedef struct {
    bool open;
    int active_field;
    char owner[8];
    char controller[8];
    char cores[512];
    bool keep_existing_cores;
    char error[512];
} EditDialog;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *map_texture;
    TTF_Font *font;
    TTF_Font *font_small;
    Hoi4Map map;
    char game_root[CP_PATH_MAX];
    char mod_root[CP_PATH_MAX];
    char status[512];
    float zoom;
    float offset_x;
    float offset_y;
    int hover_entity;
    Hoi4ViewMode view_mode;
    uint8_t selected[3][HOI4_MAX_STATES];
    size_t selected_count[3];
    bool dragging;
    bool drag_moved;
    float drag_start_x;
    float drag_start_y;
    float last_mouse_x;
    float last_mouse_y;
    EditDialog edit_dialog;
} App;

static void draw_text(App *app, TTF_Font *font, const char *text, float x, float y, SDL_Color color)
{
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_FRect target;
    if (!font || !text || !text[0]) return;
    surface = TTF_RenderText_Blended(font, text, 0, color);
    if (!surface) return;
    texture = SDL_CreateTextureFromSurface(app->renderer, surface);
    if (texture) {
        target.x = x;
        target.y = y;
        target.w = (float)surface->w;
        target.h = (float)surface->h;
        SDL_RenderTexture(app->renderer, texture, NULL, &target);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}

static TTF_Font *open_system_font(float size)
{
    static const char *fonts[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf"
    };
    size_t i;
    for (i = 0; i < sizeof(fonts) / sizeof(fonts[0]); ++i) {
        TTF_Font *font = TTF_OpenFont(fonts[i], size);
        if (font) return font;
    }
    return NULL;
}

static void fit_map(App *app)
{
    int width, height;
    float available_height;
    if (!app->map.width || !app->map.height) return;
    SDL_GetWindowSize(app->window, &width, &height);
    available_height = (float)height - TOOLBAR_HEIGHT;
    app->zoom = SDL_min((float)width / app->map.width, available_height / app->map.height);
    app->offset_x = ((float)width - app->map.width * app->zoom) * 0.5f;
    app->offset_y = TOOLBAR_HEIGHT + (available_height - app->map.height * app->zoom) * 0.5f;
}

static bool create_map_texture(App *app)
{
    if (app->map_texture) SDL_DestroyTexture(app->map_texture);
    app->map_texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         app->map.width, app->map.height);
    if (!app->map_texture) {
        snprintf(app->status, sizeof(app->status), "Erreur texture SDL : %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(app->map_texture, SDL_SCALEMODE_NEAREST);
    if (!SDL_UpdateTexture(app->map_texture, NULL, app->map.pixels, app->map.width * 4)) {
        snprintf(app->status, sizeof(app->status), "Erreur d'envoi de la carte : %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool load_map(App *app)
{
    memset(app->selected, 0, sizeof(app->selected));
    memset(app->selected_count, 0, sizeof(app->selected_count));
    snprintf(app->status, sizeof(app->status), "Chargement de la carte...");
    SDL_SetWindowTitle(app->window, "Crispy Pandas — chargement...");
    if (!hoi4_map_load(&app->map, app->game_root, app->mod_root)) {
        snprintf(app->status, sizeof(app->status), "Échec : %.500s", app->map.error);
        SDL_SetWindowTitle(app->window, "Crispy Pandas — erreur de chargement");
        return false;
    }
    if (!create_map_texture(app)) return false;
    fit_map(app);
    app->hover_entity = 0;
    app->view_mode = HOI4_VIEW_STATES;
    snprintf(app->status, sizeof(app->status), "%zu états — %zu provinces — %zu régions stratégiques — %dx%d",
             app->map.loaded_state_count, app->map.loaded_province_count,
             app->map.loaded_strategic_region_count,
             app->map.width, app->map.height);
    SDL_SetWindowTitle(app->window, "Crispy Pandas — Visualisateur de carte HOI4");
    return true;
}

static void update_entity_texture(App *app, int entity_id)
{
    Hoi4Bounds bounds;
    SDL_Rect rect;
    if (!app->map_texture
        || !hoi4_map_entity_bounds(&app->map, app->view_mode, entity_id, &bounds)) return;
    rect.x = bounds.min_x;
    rect.y = bounds.min_y;
    rect.w = bounds.max_x - bounds.min_x + 1;
    rect.h = bounds.max_y - bounds.min_y + 1;
    SDL_UpdateTexture(app->map_texture, &rect,
                      app->map.pixels + (size_t)rect.y * app->map.width + rect.x,
                      app->map.width * 4);
}

static int entity_under_mouse(const App *app, float mouse_x, float mouse_y)
{
    int x, y;
    if (!app->map.state_at || mouse_y < TOOLBAR_HEIGHT || app->zoom <= 0) return 0;
    x = (int)((mouse_x - app->offset_x) / app->zoom);
    y = (int)((mouse_y - app->offset_y) / app->zoom);
    if (x < 0 || y < 0 || x >= app->map.width || y >= app->map.height) return 0;
    return hoi4_map_entity_at(&app->map, app->view_mode, x, y);
}

static void set_hover(App *app, int state_id)
{
    int previous;
    if (state_id == app->hover_entity) return;
    previous = app->hover_entity;
    hoi4_map_paint_entity(&app->map, app->view_mode, previous,
                          previous > 0 && app->selected[app->view_mode][previous], false);
    hoi4_map_paint_entity(&app->map, app->view_mode, state_id,
                          state_id > 0 && app->selected[app->view_mode][state_id], true);
    app->hover_entity = state_id;
    update_entity_texture(app, previous);
    update_entity_texture(app, state_id);
}

static void set_view_mode(App *app, Hoi4ViewMode mode)
{
    int id;
    if (!app->map_texture || app->view_mode == mode) return;
    set_hover(app, 0);
    app->view_mode = mode;
    hoi4_map_render_mode(&app->map, mode);
    for (id = 1; id < HOI4_MAX_STATES; ++id) {
        if (app->selected[mode][id])
            hoi4_map_paint_entity(&app->map, mode, id, true, false);
    }
    SDL_UpdateTexture(app->map_texture, NULL, app->map.pixels, app->map.width * 4);
}

static void repaint_entity(App *app, int id)
{
    if (id <= 0 || id >= HOI4_MAX_STATES) return;
    hoi4_map_paint_entity(&app->map, app->view_mode, id,
                          app->selected[app->view_mode][id],
                          app->hover_entity == id);
    update_entity_texture(app, id);
}

static void select_entity_at(App *app, float mouse_x, float mouse_y, bool extend)
{
    int id = entity_under_mouse(app, mouse_x, mouse_y);
    int i;
    uint8_t *selection = app->selected[app->view_mode];
    size_t *count = &app->selected_count[app->view_mode];
    if (!extend) {
        for (i = 1; i < HOI4_MAX_STATES; ++i) {
            if (selection[i]) {
                selection[i] = 0;
                (*count)--;
                repaint_entity(app, i);
            }
        }
    }
    if (id <= 0 || id >= HOI4_MAX_STATES) return;
    if (extend && selection[id]) {
        selection[id] = 0;
        (*count)--;
    } else if (!selection[id]) {
        selection[id] = 1;
        (*count)++;
    }
    repaint_entity(app, id);
}

static void transfer_selected_provinces_at(App *app, float mouse_x, float mouse_y)
{
    int province_id;
    int target_state_id;
    const Hoi4Province *province;
    ProvinceTransferResult result;
    if (app->view_mode != HOI4_VIEW_PROVINCES
        || app->selected_count[HOI4_VIEW_PROVINCES] == 0) return;
    province_id = entity_under_mouse(app, mouse_x, mouse_y);
    province = hoi4_map_province(&app->map, province_id);
    if (!province || !province->state_id) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Transfert impossible",
                                 "Le clic droit doit viser une province terrestre appartenant à un état.",
                                 app->window);
        return;
    }
    target_state_id = province->state_id;
    if (!province_transfer_execute(&app->map, app->mod_root,
                                   app->selected[HOI4_VIEW_PROVINCES],
                                   target_state_id, &result)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Transfert annulé",
                                 result.error, app->window);
        return;
    }
    load_map(app);
    set_view_mode(app, HOI4_VIEW_PROVINCES);
    snprintf(app->status, sizeof(app->status),
             "%zu province%s transférée%s vers l'état %u — %zu région%s stratégique%s ajustée%s.",
             result.transferred_provinces,
             result.transferred_provinces > 1 ? "s" : "",
             result.transferred_provinces > 1 ? "s" : "",
             (unsigned)target_state_id,
             result.changed_strategic_regions,
             result.changed_strategic_regions > 1 ? "s" : "",
             result.changed_strategic_regions > 1 ? "s" : "",
             result.changed_strategic_regions > 1 ? "s" : "");
}

static void create_state_from_selected_provinces(App *app)
{
    ProvinceStateCreateResult result;
    if (app->view_mode != HOI4_VIEW_PROVINCES) return;
    if (app->selected_count[HOI4_VIEW_PROVINCES] == 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,
                                 "Création impossible",
                                 "Sélectionnez une ou plusieurs provinces terrestres.",
                                 app->window);
        return;
    }
    if (!province_state_create_execute(&app->map, app->mod_root,
                                       app->selected[HOI4_VIEW_PROVINCES],
                                       &result)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Création annulée",
                                 result.error, app->window);
        return;
    }
    load_map(app);
    set_view_mode(app, HOI4_VIEW_PROVINCES);
    snprintf(app->status, sizeof(app->status),
             "État %d créé avec %zu province%s — clé %s — %zu région%s stratégique%s ajustée%s.",
             result.state_id,
             result.province_count,
             result.province_count > 1 ? "s" : "",
             result.localization_key,
             result.changed_strategic_regions,
             result.changed_strategic_regions > 1 ? "s" : "",
             result.changed_strategic_regions > 1 ? "s" : "",
             result.changed_strategic_regions > 1 ? "s" : "");
}

static void zoom_at(App *app, float mouse_x, float mouse_y, float wheel)
{
    float old_zoom = app->zoom;
    float map_x, map_y;
    float factor = wheel > 0 ? 1.25f : 0.8f;
    if (!app->map.width || old_zoom <= 0) return;
    map_x = (mouse_x - app->offset_x) / old_zoom;
    map_y = (mouse_y - app->offset_y) / old_zoom;
    app->zoom = SDL_clamp(old_zoom * factor, 0.08f, 8.0f);
    app->offset_x = mouse_x - map_x * app->zoom;
    app->offset_y = mouse_y - map_y * app->zoom;
}

static void draw_mode_button(App *app, Hoi4ViewMode mode, const char *label,
                             float x, float width)
{
    SDL_FRect rect = {x, 7, width, 30};
    SDL_Color text = {225, 232, 244, 255};
    if (app->view_mode == mode) SDL_SetRenderDrawColor(app->renderer, 48, 112, 196, 255);
    else SDL_SetRenderDrawColor(app->renderer, 44, 51, 67, 255);
    SDL_RenderFillRect(app->renderer, &rect);
    draw_text(app, app->font_small, label, x + 10, 13, text);
}

static void render_toolbar(App *app, int window_width)
{
    SDL_FRect bar = {0, 0, (float)window_width, TOOLBAR_HEIGHT};
    SDL_Color white = {235, 239, 247, 255};
    SDL_Color muted = {164, 174, 194, 255};
    char line[512];
    SDL_SetRenderDrawColor(app->renderer, 25, 30, 41, 255);
    SDL_RenderFillRect(app->renderer, &bar);
    draw_text(app, app->font, "Crispy Pandas  /  Map Viewer", 18, 8, white);
    draw_mode_button(app, HOI4_VIEW_STATES, "1  États", 250, 78);
    draw_mode_button(app, HOI4_VIEW_PROVINCES, "2  Provinces", 336, 105);
    draw_mode_button(app, HOI4_VIEW_STRATEGIC_REGIONS, "3  Régions stratégiques", 449, 166);
    draw_text(app, app->font_small,
              "G Jeu   M Mod   R Recharger   F Ajuster   molette Zoom   glisser Déplacer",
              18, 37, muted);
    if (app->hover_entity) {
        if (app->view_mode == HOI4_VIEW_STATES) {
            const Hoi4State *state = hoi4_map_state(&app->map, app->hover_entity);
            if (!state) return;
            snprintf(line, sizeof(line), "État %d — %s   |   Owner: %s   |   %zu provinces   |   Sélection: %zu",
                     state->id, state->name, state->owner[0] ? state->owner : "aucun",
                     state->province_count, app->selected_count[app->view_mode]);
            draw_text(app, app->font, line, (float)window_width * 0.48f, 10, white);
            draw_text(app, app->font_small, state->source, (float)window_width * 0.48f, 38, muted);
        } else if (app->view_mode == HOI4_VIEW_PROVINCES) {
            const Hoi4Province *province = hoi4_map_province(&app->map, app->hover_entity);
            const Hoi4State *state;
            const char *type;
            if (!province) return;
            state = hoi4_map_state(&app->map, province->state_id);
            type = province->type == HOI4_PROVINCE_LAND ? "terre"
                : province->type == HOI4_PROVINCE_SEA ? "mer"
                : province->type == HOI4_PROVINCE_LAKE ? "lac" : "inconnu";
            snprintf(line, sizeof(line), "Province %d — %s   |   État: %s   |   Owner: %s   |   Sélection: %zu",
                     province->id, type, state ? state->name : "aucun",
                     state && state->owner[0] ? state->owner : "aucun",
                     app->selected_count[app->view_mode]);
            draw_text(app, app->font, line, (float)window_width * 0.48f, 20, white);
        } else {
            const Hoi4StrategicRegion *region =
                hoi4_map_strategic_region(&app->map, app->hover_entity);
            if (!region) return;
            snprintf(line, sizeof(line), "Région stratégique %d — %s   |   %zu provinces   |   Sélection: %zu",
                     region->id, region->name, region->province_count,
                     app->selected_count[app->view_mode]);
            draw_text(app, app->font, line, (float)window_width * 0.48f, 10, white);
            draw_text(app, app->font_small, region->source,
                      (float)window_width * 0.48f, 38, muted);
        }
    } else {
        snprintf(line, sizeof(line), "%.470s   |   Sélection : %zu",
                 app->status, app->selected_count[app->view_mode]);
        draw_text(app, app->font_small, line, (float)window_width * 0.48f, 23, muted);
    }
}

static void dialog_layout(App *app, SDL_FRect *panel, SDL_FRect fields[3],
                          SDL_FRect *checkbox, SDL_FRect *cancel, SDL_FRect *apply)
{
    int width, height;
    SDL_GetWindowSize(app->window, &width, &height);
    *panel = (SDL_FRect){((float)width - 620) * 0.5f, ((float)height - 410) * 0.5f, 620, 410};
    fields[0] = (SDL_FRect){panel->x + 190, panel->y + 91, 380, 38};
    fields[1] = (SDL_FRect){panel->x + 190, panel->y + 151, 380, 38};
    fields[2] = (SDL_FRect){panel->x + 190, panel->y + 211, 380, 38};
    *checkbox = (SDL_FRect){panel->x + 190, panel->y + 274, 22, 22};
    *cancel = (SDL_FRect){panel->x + 364, panel->y + 347, 96, 38};
    *apply = (SDL_FRect){panel->x + 474, panel->y + 347, 96, 38};
}

static bool point_in_rect(float x, float y, const SDL_FRect *rect)
{
    return x >= rect->x && y >= rect->y && x < rect->x + rect->w && y < rect->y + rect->h;
}

static void draw_dialog_input(App *app, const SDL_FRect *rect, int field,
                              const char *value, const char *placeholder)
{
    SDL_Color text = {239, 242, 249, 255};
    SDL_Color muted = {128, 139, 158, 255};
    if (app->edit_dialog.active_field == field)
        SDL_SetRenderDrawColor(app->renderer, 62, 137, 224, 255);
    else
        SDL_SetRenderDrawColor(app->renderer, 69, 78, 96, 255);
    SDL_RenderFillRect(app->renderer, rect);
    {
        SDL_FRect inner = {rect->x + 2, rect->y + 2, rect->w - 4, rect->h - 4};
        SDL_SetRenderDrawColor(app->renderer, 27, 33, 45, 255);
        SDL_RenderFillRect(app->renderer, &inner);
    }
    draw_text(app, app->font, value[0] ? value : placeholder,
              rect->x + 10, rect->y + 8, value[0] ? text : muted);
    if (app->edit_dialog.active_field == field) {
        char shown[520];
        int text_width = 0;
        snprintf(shown, sizeof(shown), "%s", value);
        TTF_GetStringSize(app->font, shown, 0, &text_width, NULL);
        SDL_SetRenderDrawColor(app->renderer, 225, 232, 244, 255);
        SDL_RenderLine(app->renderer, rect->x + 11 + text_width, rect->y + 8,
                       rect->x + 11 + text_width, rect->y + 29);
    }
}

static void render_edit_dialog(App *app)
{
    SDL_FRect panel, fields[3], checkbox, cancel, apply;
    SDL_Color white = {238, 242, 250, 255};
    SDL_Color muted = {164, 174, 194, 255};
    char title[160];
    if (!app->edit_dialog.open) return;
    dialog_layout(app, &panel, fields, &checkbox, &cancel, &apply);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    {
        int width, height;
        SDL_FRect overlay;
        SDL_GetWindowSize(app->window, &width, &height);
        overlay = (SDL_FRect){0, 0, (float)width, (float)height};
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 155);
        SDL_RenderFillRect(app->renderer, &overlay);
    }
    SDL_SetRenderDrawColor(app->renderer, 31, 37, 50, 255);
    SDL_RenderFillRect(app->renderer, &panel);
    snprintf(title, sizeof(title), "Modifier %zu état%s",
             app->selected_count[HOI4_VIEW_STATES],
             app->selected_count[HOI4_VIEW_STATES] > 1 ? "s" : "");
    draw_text(app, app->font, title, panel.x + 28, panel.y + 22, white);
    draw_text(app, app->font_small,
              "Un champ vide reste inchangé. Owner et controller acceptent un seul tag.",
              panel.x + 28, panel.y + 53, muted);
    draw_text(app, app->font, "Owner", panel.x + 28, panel.y + 99, white);
    draw_text(app, app->font, "Controller", panel.x + 28, panel.y + 159, white);
    draw_text(app, app->font, "Cores", panel.x + 28, panel.y + 219, white);
    draw_dialog_input(app, &fields[0], 0, app->edit_dialog.owner, "ex. GER");
    draw_dialog_input(app, &fields[1], 1, app->edit_dialog.controller, "ex. GER");
    draw_dialog_input(app, &fields[2], 2, app->edit_dialog.cores, "ex. GER, ITA, POL");
    SDL_SetRenderDrawColor(app->renderer, 78, 89, 109, 255);
    SDL_RenderFillRect(app->renderer, &checkbox);
    if (app->edit_dialog.keep_existing_cores) {
        SDL_SetRenderDrawColor(app->renderer, 70, 157, 91, 255);
        SDL_FRect checked = {checkbox.x + 4, checkbox.y + 4, 14, 14};
        SDL_RenderFillRect(app->renderer, &checked);
    }
    draw_text(app, app->font_small,
              "Garder les cores existants (décoché = les remplacer)",
              checkbox.x + 32, checkbox.y + 2, white);
    if (app->edit_dialog.error[0])
        draw_text(app, app->font_small, app->edit_dialog.error,
                  panel.x + 28, panel.y + 313, (SDL_Color){244, 112, 112, 255});
    SDL_SetRenderDrawColor(app->renderer, 65, 74, 91, 255);
    SDL_RenderFillRect(app->renderer, &cancel);
    SDL_SetRenderDrawColor(app->renderer, 48, 112, 196, 255);
    SDL_RenderFillRect(app->renderer, &apply);
    draw_text(app, app->font_small, "Annuler", cancel.x + 22, cancel.y + 10, white);
    draw_text(app, app->font_small, "Appliquer", apply.x + 18, apply.y + 10, white);
}

static void render(App *app)
{
    int width, height;
    SDL_GetWindowSize(app->window, &width, &height);
    SDL_SetRenderDrawColor(app->renderer, 9, 12, 18, 255);
    SDL_RenderClear(app->renderer);
    if (app->map_texture) {
        SDL_FRect target = {
            app->offset_x, app->offset_y,
            app->map.width * app->zoom, app->map.height * app->zoom
        };
        SDL_RenderTexture(app->renderer, app->map_texture, NULL, &target);
    }
    render_toolbar(app, width);
    render_edit_dialog(app);
    SDL_RenderPresent(app->renderer);
}

static void choose_game(App *app)
{
    char selected[CP_PATH_MAX];
    if (cp_choose_folder("Choisir le dossier d'installation de Hearts of Iron IV",
                         app->game_root, selected, sizeof(selected))) {
        snprintf(app->game_root, sizeof(app->game_root), "%s", selected);
        cp_save_settings(app->game_root, app->mod_root);
        load_map(app);
    }
}

static void choose_mod(App *app)
{
    char selected[CP_PATH_MAX];
    if (cp_choose_folder("Choisir le dossier du mod HOI4 (Annuler = conserver le dossier actuel)",
                         app->mod_root, selected, sizeof(selected))) {
        snprintf(app->mod_root, sizeof(app->mod_root), "%s", selected);
        cp_save_settings(app->game_root, app->mod_root);
        load_map(app);
    }
}

static void handle_key(App *app, SDL_Keycode key)
{
    if (key == SDLK_G) choose_game(app);
    else if (key == SDLK_M) choose_mod(app);
    else if (key == SDLK_R) load_map(app);
    else if (key == SDLK_F || key == SDLK_HOME) fit_map(app);
    else if (key == SDLK_1) set_view_mode(app, HOI4_VIEW_STATES);
    else if (key == SDLK_2) set_view_mode(app, HOI4_VIEW_PROVINCES);
    else if (key == SDLK_3) set_view_mode(app, HOI4_VIEW_STRATEGIC_REGIONS);
}

static void open_state_at(App *app, float mouse_x, float mouse_y)
{
    int state_id;
    const Hoi4State *state;
    if (app->view_mode != HOI4_VIEW_STATES) return;
    state_id = entity_under_mouse(app, mouse_x, mouse_y);
    state = hoi4_map_state(&app->map, state_id);
    if (!state) return;
    if (cp_open_file(state->source)) {
        snprintf(app->status, sizeof(app->status), "Fichier ouvert : %.490s", state->source);
    } else {
        snprintf(app->status, sizeof(app->status), "Impossible d'ouvrir : %.488s", state->source);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Crispy Pandas",
                                 app->status, app->window);
    }
}

static void close_edit_dialog(App *app)
{
    app->edit_dialog.open = false;
    SDL_StopTextInput(app->window);
}

static void open_edit_dialog(App *app)
{
    if (app->view_mode != HOI4_VIEW_STATES
        || app->selected_count[HOI4_VIEW_STATES] == 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Crispy Pandas",
                                 "Sélectionne au moins un état avant d'utiliser Ctrl+Entrée.",
                                 app->window);
        return;
    }
    memset(&app->edit_dialog, 0, sizeof(app->edit_dialog));
    app->edit_dialog.open = true;
    app->edit_dialog.active_field = 0;
    SDL_StartTextInput(app->window);
}

static void apply_edit_dialog(App *app)
{
    StateEditRequest request;
    char error[512] = "";
    char target[CP_PATH_MAX];
    int id;
    size_t edited = 0, failed = 0;
    if (!state_edit_build_request(&request,
                                  app->edit_dialog.owner,
                                  app->edit_dialog.controller,
                                  app->edit_dialog.cores,
                                  app->edit_dialog.keep_existing_cores,
                                  error, sizeof(error))) {
        snprintf(app->edit_dialog.error, sizeof(app->edit_dialog.error), "%s", error);
        return;
    }
    for (id = 1; id < HOI4_MAX_STATES; ++id) {
        const Hoi4State *state;
        if (!app->selected[HOI4_VIEW_STATES][id]) continue;
        state = hoi4_map_state(&app->map, id);
        if (!state || !state_edit_file(state->source, app->mod_root, &request,
                                       target, sizeof(target), error, sizeof(error))) {
            failed++;
        } else {
            edited++;
        }
    }
    close_edit_dialog(app);
    load_map(app);
    if (failed == 0) {
        snprintf(app->status, sizeof(app->status), "%zu état%s modifié%s dans le mod.",
                 edited, edited > 1 ? "s" : "", edited > 1 ? "s" : "");
    } else {
        char message[768];
        snprintf(message, sizeof(message),
                 "%zu état(s) modifié(s), %zu échec(s).\n\nDernière erreur : %.500s",
                 edited, failed, error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Modification partielle",
                                 message, app->window);
    }
}

static void dialog_append_text(EditDialog *dialog, const char *text)
{
    char *target;
    size_t capacity;
    size_t length;
    if (dialog->active_field == 0) {
        target = dialog->owner; capacity = sizeof(dialog->owner);
    } else if (dialog->active_field == 1) {
        target = dialog->controller; capacity = sizeof(dialog->controller);
    } else {
        target = dialog->cores; capacity = sizeof(dialog->cores);
    }
    length = strlen(target);
    while (*text && length + 1 < capacity) {
        unsigned char c = (unsigned char)*text++;
        if (dialog->active_field < 2) {
            if (isalnum(c) && length < 3) target[length++] = (char)toupper(c);
        } else if (isalnum(c) || c == ',' || c == ';' || c == ' ' || c == '\t') {
            target[length++] = isalpha(c) ? (char)toupper(c) : (char)c;
        }
    }
    target[length] = '\0';
}

static bool handle_edit_dialog_event(App *app, const SDL_Event *event)
{
    SDL_FRect panel, fields[3], checkbox, cancel, apply;
    EditDialog *dialog = &app->edit_dialog;
    if (!dialog->open) return false;
    dialog_layout(app, &panel, fields, &checkbox, &cancel, &apply);
    if (event->type == SDL_EVENT_TEXT_INPUT) {
        dialog_append_text(dialog, event->text.text);
        dialog->error[0] = '\0';
    } else if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            close_edit_dialog(app);
        } else if (event->key.key == SDLK_TAB) {
            dialog->active_field = (dialog->active_field + 1) % 3;
        } else if (event->key.key == SDLK_BACKSPACE) {
            char *target = dialog->active_field == 0 ? dialog->owner
                : dialog->active_field == 1 ? dialog->controller : dialog->cores;
            size_t length = strlen(target);
            if (length) target[length - 1] = '\0';
            dialog->error[0] = '\0';
        } else if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) {
            apply_edit_dialog(app);
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN
               && event->button.button == SDL_BUTTON_LEFT) {
        if (point_in_rect(event->button.x, event->button.y, &fields[0])) dialog->active_field = 0;
        else if (point_in_rect(event->button.x, event->button.y, &fields[1])) dialog->active_field = 1;
        else if (point_in_rect(event->button.x, event->button.y, &fields[2])) dialog->active_field = 2;
        else if (point_in_rect(event->button.x, event->button.y, &checkbox))
            dialog->keep_existing_cores = !dialog->keep_existing_cores;
        else if (point_in_rect(event->button.x, event->button.y, &cancel)) close_edit_dialog(app);
        else if (point_in_rect(event->button.x, event->button.y, &apply)) apply_edit_dialog(app);
    }
    return true;
}

static void parse_arguments(App *app, int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            snprintf(app->game_root, sizeof(app->game_root), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--mod") == 0 && i + 1 < argc) {
            snprintf(app->mod_root, sizeof(app->mod_root), "%s", argv[++i]);
        }
    }
}

int main(int argc, char **argv)
{
    App app;
    bool running = true;
    SDL_Event event;
    memset(&app, 0, sizeof(app));
    hoi4_map_init(&app.map);
    cp_load_settings(app.game_root, sizeof(app.game_root),
                     app.mod_root, sizeof(app.mod_root));
    parse_arguments(&app, argc, argv);
    if (!app.game_root[0] || !cp_path_is_dir(app.game_root))
        cp_find_hoi4(app.game_root, sizeof(app.game_root));
    if (!app.mod_root[0] || !cp_path_is_dir(app.mod_root)) {
        char selected[CP_PATH_MAX];
        if (!cp_choose_folder("Sélectionne le dossier du mod HOI4 pour continuer",
                              app.mod_root, selected, sizeof(selected))) {
            MessageBoxA(NULL,
                        "Un dossier de mod doit être sélectionné pour utiliser le visualisateur.",
                        "Crispy Pandas", MB_OK | MB_ICONINFORMATION);
            hoi4_map_free(&app.map);
            return 0;
        }
        snprintf(app.mod_root, sizeof(app.mod_root), "%s", selected);
    }
    cp_save_settings(app.game_root, app.mod_root);

    if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Crispy Pandas",
                                 SDL_GetError(), NULL);
        return 1;
    }
    if (!SDL_CreateWindowAndRenderer("Crispy Pandas — Visualisateur de carte HOI4",
                                     WINDOW_WIDTH, WINDOW_HEIGHT,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &app.window, &app.renderer)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Crispy Pandas", SDL_GetError(), NULL);
        TTF_Quit(); SDL_Quit(); return 1;
    }
    SDL_SetRenderVSync(app.renderer, 1);
    app.font = open_system_font(18);
    app.font_small = open_system_font(13);
    if (!app.game_root[0]) {
        snprintf(app.status, sizeof(app.status), "HOI4 introuvable : appuie sur G pour choisir son dossier.");
    } else {
        load_map(&app);
    }

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                continue;
            }
            if (app.edit_dialog.open) {
                handle_edit_dialog_event(&app, &event);
                continue;
            }
            switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
                if ((event.key.mod & SDL_KMOD_CTRL)
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER))
                    open_edit_dialog(&app);
                else
                    handle_key(&app, event.key.key);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT && event.button.clicks >= 2
                    && event.button.y >= TOOLBAR_HEIGHT) {
                    app.dragging = false;
                    open_state_at(&app, event.button.x, event.button.y);
                } else if (event.button.y < 42) {
                    if (event.button.x >= 250 && event.button.x < 328)
                        set_view_mode(&app, HOI4_VIEW_STATES);
                    else if (event.button.x >= 336 && event.button.x < 441)
                        set_view_mode(&app, HOI4_VIEW_PROVINCES);
                    else if (event.button.x >= 449 && event.button.x < 615)
                        set_view_mode(&app, HOI4_VIEW_STRATEGIC_REGIONS);
                } else if (event.button.y >= TOOLBAR_HEIGHT
                    && (event.button.button == SDL_BUTTON_LEFT
                        || event.button.button == SDL_BUTTON_MIDDLE
                        || event.button.button == SDL_BUTTON_RIGHT)) {
                    app.dragging = true;
                    app.drag_moved = false;
                    app.drag_start_x = event.button.x;
                    app.drag_start_y = event.button.y;
                    app.last_mouse_x = event.button.x;
                    app.last_mouse_y = event.button.y;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT && app.dragging
                    && !app.drag_moved) {
                    SDL_Keymod modifiers = SDL_GetModState();
                    if ((modifiers & SDL_KMOD_ALT)
                        && app.view_mode == HOI4_VIEW_PROVINCES)
                        create_state_from_selected_provinces(&app);
                    else {
                        bool extend = (modifiers & SDL_KMOD_SHIFT) != 0;
                        select_entity_at(&app, event.button.x, event.button.y, extend);
                    }
                } else if (event.button.button == SDL_BUTTON_RIGHT && app.dragging
                           && !app.drag_moved) {
                    transfer_selected_provinces_at(&app, event.button.x, event.button.y);
                }
                app.dragging = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (app.dragging) {
                    if (SDL_fabsf(event.motion.x - app.drag_start_x) > 2.0f
                        || SDL_fabsf(event.motion.y - app.drag_start_y) > 2.0f)
                        app.drag_moved = true;
                    app.offset_x += event.motion.x - app.last_mouse_x;
                    app.offset_y += event.motion.y - app.last_mouse_y;
                    app.last_mouse_x = event.motion.x;
                    app.last_mouse_y = event.motion.y;
                } else {
                    set_hover(&app, entity_under_mouse(&app, event.motion.x, event.motion.y));
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL: {
                float mx, my;
                SDL_GetMouseState(&mx, &my);
                zoom_at(&app, mx, my, event.wheel.y);
                set_hover(&app, entity_under_mouse(&app, mx, my));
                break;
            }
            case SDL_EVENT_WINDOW_RESIZED:
                break;
            default:
                break;
            }
        }
        render(&app);
    }

    hoi4_map_free(&app.map);
    if (app.font_small) TTF_CloseFont(app.font_small);
    if (app.font) TTF_CloseFont(app.font);
    if (app.map_texture) SDL_DestroyTexture(app.map_texture);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
