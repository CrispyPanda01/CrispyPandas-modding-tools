#ifndef CRISPY_PROVINCE_TRANSFER_H
#define CRISPY_PROVINCE_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hoi4_map.h"

typedef struct {
    size_t transferred_provinces;
    size_t changed_states;
    size_t changed_strategic_regions;
    char error[768];
} ProvinceTransferResult;

bool province_transfer_execute(const Hoi4Map *map,
                               const char *mod_root,
                               const uint8_t *selected_provinces,
                               int target_state_id,
                               ProvinceTransferResult *result);

bool province_transfer_replace_id_block(const char *input, const char *block_name,
                                        const int *ids, size_t id_count,
                                        char **output,
                                        char *error, size_t error_size);

bool province_transfer_validate_syntax(const char *text,
                                       char *error, size_t error_size);

#endif
