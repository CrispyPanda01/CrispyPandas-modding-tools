#ifndef CRISPY_CHARACTER_CREATOR_H
#define CRISPY_CHARACTER_CREATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "path_utils.h"

enum {
    CHARACTER_ROLE_COUNTRY_LEADER = 1u << 0,
    CHARACTER_ROLE_ADVISOR        = 1u << 1,
    CHARACTER_ROLE_GENERAL        = 1u << 2,
    CHARACTER_ROLE_FIELD_MARSHAL  = 1u << 3,
    CHARACTER_ROLE_NAVY_LEADER    = 1u << 4,
    CHARACTER_ROLE_SCIENTIST      = 1u << 5
};

typedef struct {
    char country_tag[8];
    char token[128];
    char name[192];
    uint32_t roles;

    char ideology[96];
    char expire[32];
    char advisor_slot[64];
    int advisor_cost;
    char advisor_ledger[32];

    int land_skill;
    int attack_skill;
    int defense_skill;
    int planning_skill;
    int logistics_skill;

    int navy_skill;
    int navy_attack_skill;
    int navy_defense_skill;
    int maneuvering_skill;
    int coordination_skill;

    char country_traits[2048];
    char advisor_traits[2048];
    char land_traits[2048];
    char navy_traits[2048];
    char scientist_traits[2048];
    char scientist_specialization[96];
    int scientist_skill;

    char large_portrait[CP_PATH_MAX];
    char small_portrait[CP_PATH_MAX];
} CharacterCreateRequest;

typedef struct {
    char character_file[CP_PATH_MAX];
    char gfx_file[CP_PATH_MAX];
    char history_file[CP_PATH_MAX];
    char large_portrait_file[CP_PATH_MAX];
    char small_portrait_file[CP_PATH_MAX];
    size_t changed_files;
    char error[768];
} CharacterCreateResult;

void character_create_request_defaults(CharacterCreateRequest *request);
bool character_create_validate(const CharacterCreateRequest *request,
                               char *error, size_t error_size);
bool character_create_execute(const char *game_root, const char *mod_root,
                              const CharacterCreateRequest *request,
                              CharacterCreateResult *result);

typedef struct {
    char token[128];
    uint32_t roles;
} CharacterTrait;

typedef struct {
    CharacterTrait *items;
    size_t count;
    size_t capacity;
    char error[512];
} CharacterTraitCatalog;

void character_trait_catalog_free(CharacterTraitCatalog *catalog);
bool character_trait_catalog_load(CharacterTraitCatalog *catalog,
                                  const char *game_root, const char *mod_root);

bool character_trait_list_contains(const char *list, const char *token);
bool character_trait_list_toggle(char *list, size_t size, const char *token);

#endif
