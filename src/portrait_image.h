#ifndef CRISPY_PORTRAIT_IMAGE_H
#define CRISPY_PORTRAIT_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "path_utils.h"

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
} PortraitImage;

bool portrait_image_load(const char *path, PortraitImage *image,
                         char *error, size_t error_size);
void portrait_image_free(PortraitImage *image);

bool portrait_image_resize_cover(const PortraitImage *source,
                                 int target_width, int target_height,
                                 PortraitImage *output,
                                 char *error, size_t error_size);

bool portrait_image_compose_advisor_small(const PortraitImage *source,
                                          PortraitImage *output,
                                          char *error, size_t error_size);

bool portrait_image_make_dds(const PortraitImage *image,
                             uint8_t **data, size_t *data_size,
                             char *error, size_t error_size);

#endif
