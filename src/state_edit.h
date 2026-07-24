#ifndef CRISPY_STATE_EDIT_H
#define CRISPY_STATE_EDIT_H

#include <stdbool.h>
#include <stddef.h>

#include "path_utils.h"

#define STATE_EDIT_MAX_CORES 64

typedef struct {
    char owner[8];
    char controller[8];
    char cores[STATE_EDIT_MAX_CORES][8];
    size_t core_count;
    bool keep_existing_cores;
} StateEditRequest;

bool state_edit_build_request(StateEditRequest *request,
                              const char *owner,
                              const char *controller,
                              const char *cores,
                              bool keep_existing_cores,
                              char *error, size_t error_size);
bool state_edit_transform(const char *input, const StateEditRequest *request,
                          char **output, char *error, size_t error_size);
bool state_edit_file(const char *source, const char *mod_root,
                     const StateEditRequest *request,
                     char *target, size_t target_size,
                     char *error, size_t error_size);

#endif
