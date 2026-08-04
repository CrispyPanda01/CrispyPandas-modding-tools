#ifndef CRISPY_HOI4_MAP_H
#define CRISPY_HOI4_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "path_utils.h"

#define HOI4_MAX_STATES 65536
#define HOI4_MAX_PROVINCES 65536
#define HOI4_MAX_COUNTRIES 4096

typedef enum {
    HOI4_VIEW_STATES,
    HOI4_VIEW_PROVINCES,
    HOI4_VIEW_STRATEGIC_REGIONS
} Hoi4ViewMode;

typedef enum {
    HOI4_PROVINCE_UNKNOWN,
    HOI4_PROVINCE_LAND,
    HOI4_PROVINCE_SEA,
    HOI4_PROVINCE_LAKE
} Hoi4ProvinceType;

typedef struct {
    int id;
    uint8_t r, g, b;
    Hoi4ProvinceType type;
    uint16_t state_id;
    uint16_t strategic_region_id;
    int min_x, min_y, max_x, max_y;
} Hoi4Province;

typedef struct {
    int province_id;
    int value;
    int map_x;
    int map_y;
} Hoi4VictoryPoint;

typedef struct {
    int id;
    char name[160];
    char owner[8];
    char source[CP_PATH_MAX];
    int *provinces;
    size_t province_count;
    Hoi4VictoryPoint *victory_points;
    size_t victory_point_count;
    int min_x, min_y, max_x, max_y;
} Hoi4State;

typedef struct {
    int id;
    char name[160];
    char source[CP_PATH_MAX];
    int *provinces;
    size_t province_count;
    int min_x, min_y, max_x, max_y;
} Hoi4StrategicRegion;

typedef struct {
    char tag[8];
    uint8_t r, g, b;
} Hoi4Country;

typedef struct {
    int width;
    int height;
    uint16_t *province_at;
    uint16_t *state_at;
    uint32_t *pixels;
    uint32_t *base_pixels;
    Hoi4State **states;
    Hoi4StrategicRegion **strategic_regions;
    Hoi4Province *provinces;
    Hoi4Country *countries;
    size_t country_count;
    size_t country_capacity;
    char game_root[CP_PATH_MAX];
    char mod_root[CP_PATH_MAX];
    char error[512];
    size_t loaded_state_count;
    size_t loaded_strategic_region_count;
    size_t loaded_province_count;
    Hoi4ViewMode view_mode;
} Hoi4Map;

typedef struct {
    int min_x, min_y, max_x, max_y;
} Hoi4Bounds;

void hoi4_map_init(Hoi4Map *map);
void hoi4_map_free(Hoi4Map *map);
bool hoi4_map_load(Hoi4Map *map, const char *game_root, const char *mod_root);
const Hoi4State *hoi4_map_state(const Hoi4Map *map, int id);
const Hoi4Province *hoi4_map_province(const Hoi4Map *map, int id);
const Hoi4StrategicRegion *hoi4_map_strategic_region(const Hoi4Map *map, int id);
int hoi4_map_entity_at(const Hoi4Map *map, Hoi4ViewMode mode, int x, int y);
bool hoi4_map_entity_bounds(const Hoi4Map *map, Hoi4ViewMode mode, int id, Hoi4Bounds *bounds);
void hoi4_map_render_mode(Hoi4Map *map, Hoi4ViewMode mode);
void hoi4_map_set_hover(Hoi4Map *map, Hoi4ViewMode mode, int previous, int current);
void hoi4_map_paint_entity(Hoi4Map *map, Hoi4ViewMode mode, int id,
                           bool selected, bool hovered);

#endif
