#ifndef CRISPY_COUNTRY_CREATOR_H
#define CRISPY_COUNTRY_CREATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "path_utils.h"

typedef enum {
    COUNTRY_CULTURE_WESTERN_EUROPEAN,
    COUNTRY_CULTURE_EASTERN_EUROPEAN,
    COUNTRY_CULTURE_ASIAN,
    COUNTRY_CULTURE_AFRICAN,
    COUNTRY_CULTURE_SOUTH_AMERICAN,
    COUNTRY_CULTURE_MIDDLE_EASTERN,
    COUNTRY_CULTURE_COMMONWEALTH,
    COUNTRY_CULTURE_COUNT
} CountryCulture;

typedef enum {
    COUNTRY_IDEOLOGY_NEUTRALITY,
    COUNTRY_IDEOLOGY_DEMOCRATIC,
    COUNTRY_IDEOLOGY_FASCISM,
    COUNTRY_IDEOLOGY_COMMUNISM,
    COUNTRY_IDEOLOGY_COUNT
} CountryIdeology;

typedef struct {
    char tag[4];
    char name[128];
    char adjective[128];
    char definite_name[160];
    CountryCulture culture;
    int color[3];
    int color_ui[3];
    int capital;
    CountryIdeology ruling_party;
    bool create_placeholder_flags;
} CountryCreateRequest;

typedef struct {
    char tag_file[CP_PATH_MAX];
    char colors_file[CP_PATH_MAX];
    char history_file[CP_PATH_MAX];
    char localisation_file[CP_PATH_MAX];
    char flag_files[3][CP_PATH_MAX];
    size_t changed_files;
    char error[768];
} CountryCreateResult;

void country_create_request_defaults(CountryCreateRequest *request);
const char *country_culture_label(CountryCulture culture);
const char *country_culture_file(CountryCulture culture);
const char *country_ideology_name(CountryIdeology ideology);

bool country_creator_validate(const CountryCreateRequest *request,
                              const char *game_root, const char *mod_root,
                              char *error, size_t error_size);
bool country_creator_execute(const CountryCreateRequest *request,
                             const char *game_root, const char *mod_root,
                             CountryCreateResult *result);

#endif
