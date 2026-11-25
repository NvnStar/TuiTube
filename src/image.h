#ifndef TUITUBE_IMAGE_H
#define TUITUBE_IMAGE_H

#include "model.h"

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THUMB_CACHE_MAX_MEM 256

typedef struct {
    int width;
    int height;
    int stride;
    uint8_t *pixels;
} ImageBuffer;

typedef struct ThumbCache ThumbCache;

bool image_decode_bytes(const uint8_t *data, size_t len, ImageBuffer *out);
void image_buffer_free(ImageBuffer *buf);
cairo_surface_t *image_buffer_to_cairo(ImageBuffer *buf);
cairo_surface_t *image_load_cairo_file(const char *path);

void image_resolve_thumb_url(ItemKind kind, const char *id, const char *raw_url,
                             char *out, size_t out_cap);

ThumbCache *thumb_cache_create(const char *cache_dir);
void thumb_cache_destroy(ThumbCache *tc);
cairo_surface_t *thumb_cache_get(ThumbCache *tc, const char *key);
void thumb_cache_put(ThumbCache *tc, const char *key, cairo_surface_t *surf);
void thumb_cache_clear(ThumbCache *tc);

uint8_t *thumb_disk_load(const char *cache_dir, const char *key, size_t *out_len);
bool thumb_disk_save(const char *cache_dir, const char *key, const uint8_t *data, size_t len);

#endif
