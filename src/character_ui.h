#ifndef CRISPY_CHARACTER_UI_H
#define CRISPY_CHARACTER_UI_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>

#include "character_creator.h"

typedef struct {
    CharacterCreateRequest request;
    CharacterTraitCatalog traits;
    SDL_Texture *large_preview;
    SDL_Texture *small_preview;
    int active_field;
    int trait_role;
    char trait_search[128];
    float form_scroll;
    float trait_scroll;
    float content_height;
    int portrait_drop_target;
    bool token_manual;
    char status[768];
} CharacterCreatorUI;

void character_ui_init(CharacterCreatorUI *ui,
                       const char *game_root, const char *mod_root);
void character_ui_reload(CharacterCreatorUI *ui,
                         const char *game_root, const char *mod_root);
void character_ui_free(CharacterCreatorUI *ui);

void character_ui_render(CharacterCreatorUI *ui,
                         SDL_Window *window, SDL_Renderer *renderer,
                         TTF_Font *font, TTF_Font *font_small);

bool character_ui_handle_event(CharacterCreatorUI *ui,
                               SDL_Window *window, SDL_Renderer *renderer,
                               const SDL_Event *event,
                               const char *game_root, const char *mod_root);

#endif
