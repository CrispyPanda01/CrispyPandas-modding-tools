#define COBJMACROS

#include "portrait_image.h"

#include <windows.h>
#include <wincodec.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void release_unknown(IUnknown *object)
{
    if (object) IUnknown_Release(object);
}

bool portrait_image_load(const char *path, PortraitImage *image,
                         char *error, size_t error_size)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    wchar_t wide_path[CP_PATH_MAX];
    UINT width = 0, height = 0;
    HRESULT hr;
    bool initialized = false;
    bool success = false;

    memset(image, 0, sizeof(*image));
    if (!path || !path[0]
        || !MultiByteToWideChar(CP_ACP, 0, path, -1, wide_path,
                                (int)(sizeof(wide_path) / sizeof(wide_path[0])))) {
        snprintf(error, error_size, "Chemin d'image invalide.");
        return false;
    }
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    initialized = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        snprintf(error, error_size, "Impossible d'initialiser le lecteur d'images.");
        return false;
    }
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateDecoderFromFilename(
            factory, wide_path, NULL, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameDecode_GetSize(frame, &width, &height);
    if (SUCCEEDED(hr) && (!width || !height
        || width > 16384 || height > 16384)) hr = E_INVALIDARG;
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(hr))
        hr = IWICFormatConverter_Initialize(
            converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
        size_t bytes = (size_t)width * height * 4;
        image->pixels = malloc(bytes);
        if (!image->pixels) hr = E_OUTOFMEMORY;
        else
            hr = IWICFormatConverter_CopyPixels(converter, NULL, width * 4,
                                                (UINT)bytes, image->pixels);
    }
    if (SUCCEEDED(hr)) {
        image->width = (int)width;
        image->height = (int)height;
        success = true;
    } else {
        free(image->pixels);
        memset(image, 0, sizeof(*image));
        snprintf(error, error_size,
                 "Impossible de lire l'image. Utilisez PNG, JPEG, BMP ou TIFF.");
    }
    release_unknown((IUnknown *)converter);
    release_unknown((IUnknown *)frame);
    release_unknown((IUnknown *)decoder);
    release_unknown((IUnknown *)factory);
    if (initialized) CoUninitialize();
    return success;
}

void portrait_image_free(PortraitImage *image)
{
    if (!image) return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}

static uint8_t lerp_channel(const uint8_t *pixels, int width,
                            int x0, int y0, int x1, int y1,
                            float fx, float fy, int channel)
{
    float a = pixels[((size_t)y0 * width + x0) * 4 + channel];
    float b = pixels[((size_t)y0 * width + x1) * 4 + channel];
    float c = pixels[((size_t)y1 * width + x0) * 4 + channel];
    float d = pixels[((size_t)y1 * width + x1) * 4 + channel];
    float top = a + (b - a) * fx;
    float bottom = c + (d - c) * fx;
    float value = top + (bottom - top) * fy;
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    return (uint8_t)(value + 0.5f);
}

bool portrait_image_resize_cover(const PortraitImage *source,
                                 int target_width, int target_height,
                                 PortraitImage *output,
                                 char *error, size_t error_size)
{
    float scale;
    float visible_width, visible_height;
    float offset_x, offset_y;
    int x, y, c;
    memset(output, 0, sizeof(*output));
    if (!source || !source->pixels || source->width <= 0 || source->height <= 0
        || target_width <= 0 || target_height <= 0) {
        snprintf(error, error_size, "Dimensions de portrait invalides.");
        return false;
    }
    scale = fmaxf((float)target_width / source->width,
                  (float)target_height / source->height);
    visible_width = target_width / scale;
    visible_height = target_height / scale;
    offset_x = (source->width - visible_width) * 0.5f;
    offset_y = (source->height - visible_height) * 0.5f;
    output->pixels = malloc((size_t)target_width * target_height * 4);
    if (!output->pixels) {
        snprintf(error, error_size, "Mémoire insuffisante pour redimensionner le portrait.");
        return false;
    }
    output->width = target_width;
    output->height = target_height;
    for (y = 0; y < target_height; ++y) {
        float sy = offset_y + ((float)y + 0.5f) / scale - 0.5f;
        int y0, y1;
        float fy;
        if (sy < 0) sy = 0;
        if (sy > source->height - 1) sy = (float)source->height - 1;
        y0 = (int)floorf(sy);
        y1 = y0 + 1 < source->height ? y0 + 1 : y0;
        fy = sy - y0;
        for (x = 0; x < target_width; ++x) {
            float sx = offset_x + ((float)x + 0.5f) / scale - 0.5f;
            int x0, x1;
            float fx;
            if (sx < 0) sx = 0;
            if (sx > source->width - 1) sx = (float)source->width - 1;
            x0 = (int)floorf(sx);
            x1 = x0 + 1 < source->width ? x0 + 1 : x0;
            fx = sx - x0;
            for (c = 0; c < 4; ++c)
                output->pixels[((size_t)y * target_width + x) * 4 + c] =
                    lerp_channel(source->pixels, source->width,
                                 x0, y0, x1, y1, fx, fy, c);
        }
    }
    return true;
}

static bool executable_asset_path(const char *name, char *path, size_t size)
{
    char executable[CP_PATH_MAX];
    char *slash;
    DWORD length = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (!length || length >= sizeof(executable)) return false;
    slash = strrchr(executable, '\\');
    if (!slash) slash = strrchr(executable, '/');
    if (!slash) return false;
    *slash = '\0';
    return cp_path_join(path, size, executable, name);
}

static uint8_t sample_channel(const PortraitImage *source,
                              float x, float y, int channel)
{
    int x0, x1, y0, y1;
    float fx, fy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > source->width - 1) x = (float)source->width - 1;
    if (y > source->height - 1) y = (float)source->height - 1;
    x0 = (int)floorf(x);
    y0 = (int)floorf(y);
    x1 = x0 + 1 < source->width ? x0 + 1 : x0;
    y1 = y0 + 1 < source->height ? y0 + 1 : y0;
    fx = x - x0;
    fy = y - y0;
    return lerp_channel(source->pixels, source->width,
                        x0, y0, x1, y1, fx, fy, channel);
}

bool portrait_image_compose_advisor_small(const PortraitImage *source,
                                          PortraitImage *output,
                                          char *error, size_t error_size)
{
    PortraitImage frame = {0};
    char frame_path[CP_PATH_MAX];
    const float center_x = 24.5f;
    const float center_y = 31.5f;
    const float photo_width = 35.0f;
    const float photo_height = 52.0f;
    const float angle = -6.0f * 3.14159265358979323846f / 180.0f;
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    float scale;
    float visible_width, visible_height, crop_x, crop_y;
    int x, y, channel;
    memset(output, 0, sizeof(*output));
    if (!source || !source->pixels
        || !executable_asset_path("advisor_small_template.png",
                                  frame_path, sizeof(frame_path))
        || !portrait_image_load(frame_path, &frame, error, error_size)) {
        if (!error[0])
            snprintf(error, error_size,
                     "Le cadre advisor_small_template.png est introuvable.");
        return false;
    }
    if (frame.width != 65 || frame.height != 67) {
        portrait_image_free(&frame);
        snprintf(error, error_size,
                 "Le cadre de portrait small doit mesurer 65×67.");
        return false;
    }
    output->pixels = malloc((size_t)frame.width * frame.height * 4);
    if (!output->pixels) {
        portrait_image_free(&frame);
        snprintf(error, error_size,
                 "Mémoire insuffisante pour composer le portrait small.");
        return false;
    }
    output->width = frame.width;
    output->height = frame.height;
    scale = fmaxf(photo_width / source->width,
                  photo_height / source->height);
    visible_width = photo_width / scale;
    visible_height = photo_height / scale;
    crop_x = (source->width - visible_width) * 0.5f;
    crop_y = (source->height - visible_height) * 0.5f;
    for (y = 0; y < frame.height; ++y) {
        for (x = 0; x < frame.width; ++x) {
            size_t offset = ((size_t)y * frame.width + x) * 4;
            uint8_t red = frame.pixels[offset];
            uint8_t green = frame.pixels[offset + 1];
            uint8_t blue = frame.pixels[offset + 2];
            int maximum = red > blue ? red : blue;
            float mask = 0.0f;
            if (green > 30
                && green > red * 1.20f
                && green > blue * 1.20f) {
                mask = (green - maximum - 2) / 35.0f;
                if (mask < 0) mask = 0;
                if (mask > 1) mask = 1;
            }
            if (mask > 0) {
                float dx = x - center_x;
                float dy = y - center_y;
                float local_x = cosine * dx + sine * dy;
                float local_y = -sine * dx + cosine * dy;
                float source_x = crop_x
                    + (local_x + photo_width * 0.5f) / scale;
                float source_y = crop_y
                    + (local_y + photo_height * 0.5f) / scale;
                for (channel = 0; channel < 4; ++channel) {
                    float portrait = sample_channel(source, source_x,
                                                    source_y, channel);
                    float template_value = frame.pixels[offset + channel];
                    output->pixels[offset + channel] =
                        (uint8_t)(template_value * (1.0f - mask)
                                  + portrait * mask + 0.5f);
                }
            } else {
                memcpy(output->pixels + offset, frame.pixels + offset, 4);
            }
        }
    }
    portrait_image_free(&frame);
    return true;
}

static void put_u32(uint8_t *data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1] = (uint8_t)(value >> 8);
    data[offset + 2] = (uint8_t)(value >> 16);
    data[offset + 3] = (uint8_t)(value >> 24);
}

bool portrait_image_make_dds(const PortraitImage *image,
                             uint8_t **data, size_t *data_size,
                             char *error, size_t error_size)
{
    size_t pixels_size;
    size_t i;
    uint8_t *output;
    if (!image || !image->pixels || image->width <= 0 || image->height <= 0) {
        snprintf(error, error_size, "Portrait redimensionné invalide.");
        return false;
    }
    pixels_size = (size_t)image->width * image->height * 4;
    output = calloc(128 + pixels_size, 1);
    if (!output) {
        snprintf(error, error_size, "Mémoire insuffisante pour créer le DDS.");
        return false;
    }
    memcpy(output, "DDS ", 4);
    put_u32(output, 4, 124);
    put_u32(output, 8, 0x0000100f);
    put_u32(output, 12, (uint32_t)image->height);
    put_u32(output, 16, (uint32_t)image->width);
    put_u32(output, 20, (uint32_t)image->width * 4);
    put_u32(output, 76, 32);
    put_u32(output, 80, 0x00000041);
    put_u32(output, 88, 32);
    put_u32(output, 92, 0x00ff0000);
    put_u32(output, 96, 0x0000ff00);
    put_u32(output, 100, 0x000000ff);
    put_u32(output, 104, 0xff000000);
    put_u32(output, 108, 0x00001000);
    for (i = 0; i < pixels_size; i += 4) {
        output[128 + i] = image->pixels[i + 2];
        output[128 + i + 1] = image->pixels[i + 1];
        output[128 + i + 2] = image->pixels[i];
        output[128 + i + 3] = image->pixels[i + 3];
    }
    *data = output;
    *data_size = 128 + pixels_size;
    return true;
}
