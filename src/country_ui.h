#ifndef CRISPY_COUNTRY_UI_H
#define CRISPY_COUNTRY_UI_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>

#include "country_creator.h"

typedef struct {
    CountryCreateRequest request;
    int active_field;
    char capital[16];
    char colors[6][4];
    char status[768];
} CountryCreatorUI;

void country_ui_init(CountryCreatorUI *ui);
void country_ui_reload(CountryCreatorUI *ui);
void country_ui_render(CountryCreatorUI *ui, SDL_Window *window,
                       SDL_Renderer *renderer,
                       TTF_Font *font, TTF_Font *font_small);
bool country_ui_handle_event(CountryCreatorUI *ui, SDL_Window *window,
                             const SDL_Event *event,
                             const char *game_root, const char *mod_root);

#endif
