#include "image.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>

#define MAX_IMAGE_DIM 4096

static uint8_t premul(uint8_t c, uint8_t a) {
    return (uint8_t)(((unsigned)c * (unsigned)a + 127u) / 255u);
}

static void write_argb32_pixel(uint8_t *dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t px = ((uint32_t)a << 24) |
                  ((uint32_t)premul(r, a) << 16) |
                  ((uint32_t)premul(g, a) << 8) |
                  (uint32_t)premul(b, a);
    memcpy(dst, &px, sizeof(px));
}

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf env;
} JpegErrCtx;

static void jpeg_err_exit(j_common_ptr cinfo) {
    JpegErrCtx *ctx = (JpegErrCtx *)cinfo->err;
    longjmp(ctx->env, 1);
}

static void jpeg_silent_msg(j_common_ptr cinfo) {
    (void)cinfo;
}

static bool decode_jpeg(const uint8_t *data, size_t len, ImageBuffer *out) {
    struct jpeg_decompress_struct cinfo;
    JpegErrCtx jerr;
    uint8_t * volatile pixels = NULL;

    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_err_exit;
    jerr.pub.output_message = jpeg_silent_msg;

    if (setjmp(jerr.env)) {
        jpeg_destroy_decompress(&cinfo);
        free(pixels);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, (unsigned long)len);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    if (cinfo.output_width == 0 || cinfo.output_height == 0 ||
        cinfo.output_width > MAX_IMAGE_DIM || cinfo.output_height > MAX_IMAGE_DIM ||
        cinfo.output_components != 3) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    int w = (int)cinfo.output_width;
    int h = (int)cinfo.output_height;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    pixels = malloc((size_t)stride * (size_t)h);
    uint8_t *row_buf = malloc((size_t)w * 3u);
    if (!pixels || !row_buf) {
        free(row_buf);
        free(pixels);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        int y = (int)cinfo.output_scanline;
        JSAMPROW row_ptr = row_buf;
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);
        uint8_t *dst = pixels + (size_t)y * (size_t)stride;
        for (int x = 0; x < w; x++) {
            write_argb32_pixel(dst + (size_t)x * 4u,
                               row_buf[x * 3], row_buf[x * 3 + 1], row_buf[x * 3 + 2], 255);
        }
    }

    free(row_buf);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    out->width = w;
    out->height = h;
    out->stride = stride;
    out->pixels = pixels;
    return true;
}

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} PngMemReader;

static void png_mem_read_cb(png_structp png_ptr, png_bytep out, png_size_t count) {
    PngMemReader *r = png_get_io_ptr(png_ptr);
    if (!r || r->pos + count > r->len) {
        png_error(png_ptr, "PNG read EOF");
        return;
    }
    memcpy(out, r->data + r->pos, count);
    r->pos += count;
}

static bool decode_png(const uint8_t *data, size_t len, ImageBuffer *out) {
    if (len < 8 || png_sig_cmp(data, 0, 8) != 0) return false;

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) return false;
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return false;
    }

    uint8_t * volatile pixels = NULL;
    png_bytep * volatile row_ptrs = NULL;
    uint8_t * volatile rgba = NULL;

    if (setjmp(png_jmpbuf(png_ptr))) {
        free(row_ptrs);
        free(rgba);
        free(pixels);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    PngMemReader reader = {data, len, 0};
    png_set_read_fn(png_ptr, &reader, png_mem_read_cb);
    png_read_info(png_ptr, info_ptr);

    png_uint_32 w = png_get_image_width(png_ptr, info_ptr);
    png_uint_32 h = png_get_image_height(png_ptr, info_ptr);
    if (w == 0 || h == 0 || w > MAX_IMAGE_DIM || h > MAX_IMAGE_DIM) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png_ptr, info_ptr);

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, (int)w);
    pixels = malloc((size_t)stride * (size_t)h);
    rgba = malloc((size_t)w * 4u * (size_t)h);
    row_ptrs = malloc(sizeof(png_bytep) * (size_t)h);
    if (!pixels || !rgba || !row_ptrs) {
        free(row_ptrs);
        free(rgba);
        free(pixels);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    for (png_uint_32 y = 0; y < h; y++) {
        row_ptrs[y] = rgba + (size_t)y * (size_t)w * 4u;
    }
    png_read_image(png_ptr, row_ptrs);
    png_read_end(png_ptr, NULL);

    for (png_uint_32 y = 0; y < h; y++) {
        const uint8_t *src = row_ptrs[y];
        uint8_t *dst = pixels + (size_t)y * (size_t)stride;
        for (png_uint_32 x = 0; x < w; x++) {
            write_argb32_pixel(dst + (size_t)x * 4u,
                               src[x * 4], src[x * 4 + 1], src[x * 4 + 2], src[x * 4 + 3]);
        }
    }

    free(row_ptrs);
    free(rgba);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    out->width = (int)w;
    out->height = (int)h;
    out->stride = stride;
    out->pixels = pixels;
    return true;
}

bool image_decode_bytes(const uint8_t *data, size_t len, ImageBuffer *out) {
    if (!data || !out || len < 4) return false;
    memset(out, 0, sizeof(*out));
    if (len >= 8 && png_sig_cmp(data, 0, 8) == 0) {
        return decode_png(data, len, out);
    }
    if (data[0] == 0xFF && data[1] == 0xD8) {
        return decode_jpeg(data, len, out);
    }
    return false;
}

void image_buffer_free(ImageBuffer *buf) {
    if (!buf) return;
    free(buf->pixels);
    memset(buf, 0, sizeof(*buf));
}

static cairo_user_data_key_t PIXEL_DATA_KEY;

cairo_surface_t *image_buffer_to_cairo(ImageBuffer *buf) {
    if (!buf || !buf->pixels || buf->width <= 0 || buf->height <= 0) return NULL;
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        buf->pixels, CAIRO_FORMAT_ARGB32, buf->width, buf->height, buf->stride);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        if (surf) cairo_surface_destroy(surf);
        image_buffer_free(buf);
        return NULL;
    }
    if (cairo_surface_set_user_data(surf, &PIXEL_DATA_KEY, buf->pixels, free) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        image_buffer_free(buf);
        return NULL;
    }
    buf->pixels = NULL;
    buf->width = 0;
    buf->height = 0;
    buf->stride = 0;
    return surf;
}

cairo_surface_t *image_load_cairo_file(const char *path) {
    if (!path || !path[0]) return NULL;
    size_t len = 0;
    char *raw = tt_read_file(path, &len);
    if (!raw) return NULL;
    ImageBuffer buf;
    bool ok = image_decode_bytes((const uint8_t *)raw, len, &buf);
    free(raw);
    if (!ok) return NULL;
    return image_buffer_to_cairo(&buf);
}

void image_resolve_thumb_url(ItemKind kind, const char *id, const char *raw_url,
                             char *out, size_t out_cap) {
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (kind == ITEM_VIDEO && id && id[0]) {
        snprintf(out, out_cap, "https://i.ytimg.com/vi/%s/mqdefault.jpg", id);
        return;
    }
    if (raw_url && raw_url[0]) {
        if (strncmp(raw_url, "//", 2) == 0) {
            snprintf(out, out_cap, "https:%s", raw_url);
        } else {
            snprintf(out, out_cap, "%s", raw_url);
        }
        return;
    }
    if (id && id[0] && kind == ITEM_PLAYLIST) {
        snprintf(out, out_cap, "https://i.ytimg.com/vi/%s/mqdefault.jpg", id);
    }
}

typedef struct ThumbNode {
    char *key;
    cairo_surface_t *surf;
    struct ThumbNode *prev;
    struct ThumbNode *next;
} ThumbNode;

struct ThumbCache {
    char *cache_dir;
    ThumbNode *head;
    ThumbNode *tail;
    size_t count;
};

ThumbCache *thumb_cache_create(const char *cache_dir) {
    ThumbCache *tc = calloc(1, sizeof(ThumbCache));
    if (!tc) return NULL;
    tc->cache_dir = tt_strdup(cache_dir ? cache_dir : "");
    return tc;
}

static void thumb_cache_flush_mem(ThumbCache *tc) {
    ThumbNode *n = tc->head;
    while (n) {
        ThumbNode *next = n->next;
        free(n->key);
        if (n->surf) cairo_surface_destroy(n->surf);
        free(n);
        n = next;
    }
    tc->head = NULL;
    tc->tail = NULL;
    tc->count = 0;
}

void thumb_cache_destroy(ThumbCache *tc) {
    if (!tc) return;
    thumb_cache_flush_mem(tc);
    free(tc->cache_dir);
    free(tc);
}

static void move_to_head(ThumbCache *tc, ThumbNode *n) {
    if (tc->head == n) return;
    if (n->prev) n->prev->next = n->next;
    if (n->next) n->next->prev = n->prev;
    if (tc->tail == n) tc->tail = n->prev;

    n->prev = NULL;
    n->next = tc->head;
    if (tc->head) tc->head->prev = n;
    tc->head = n;
    if (!tc->tail) tc->tail = n;
}

cairo_surface_t *thumb_cache_get(ThumbCache *tc, const char *key) {
    if (!tc || !key || !key[0]) return NULL;
    for (ThumbNode *n = tc->head; n; n = n->next) {
        if (strcmp(n->key, key) == 0) {
            move_to_head(tc, n);
            return n->surf;
        }
    }
    return NULL;
}

void thumb_cache_put(ThumbCache *tc, const char *key, cairo_surface_t *surf) {
    if (!tc || !key || !key[0] || !surf) return;
    for (ThumbNode *n = tc->head; n; n = n->next) {
        if (strcmp(n->key, key) == 0) {
            if (n->surf != surf) {
                cairo_surface_destroy(n->surf);
                n->surf = cairo_surface_reference(surf);
            }
            move_to_head(tc, n);
            return;
        }
    }

    if (tc->count >= THUMB_CACHE_MAX_MEM && tc->tail) {
        ThumbNode *victim = tc->tail;
        if (victim->prev) victim->prev->next = NULL;
        tc->tail = victim->prev;
        if (tc->head == victim) tc->head = NULL;
        tc->count--;
        free(victim->key);
        if (victim->surf) cairo_surface_destroy(victim->surf);
        free(victim);
    }

    ThumbNode *n = calloc(1, sizeof(ThumbNode));
    if (!n) return;
    n->key = tt_strdup(key);
    n->surf = cairo_surface_reference(surf);
    n->next = tc->head;
    if (tc->head) tc->head->prev = n;
    tc->head = n;
    if (!tc->tail) tc->tail = n;
    tc->count++;
}

void thumb_cache_clear(ThumbCache *tc) {
    if (!tc) return;
    thumb_cache_flush_mem(tc);
    if (!tc->cache_dir || !tc->cache_dir[0]) return;
    DIR *d = opendir(tc->cache_dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen > 4 && strcmp(ent->d_name + nlen - 4, ".img") == 0) {
            char *p = tt_path_join(tc->cache_dir, ent->d_name);
            if (p) {
                unlink(p);
                free(p);
            }
        }
    }
    closedir(d);
}

static char *disk_cache_path(const char *cache_dir, const char *key) {
    if (!cache_dir || !cache_dir[0] || !key || !key[0]) return NULL;
    char clean[128];
    tt_sanitize_filename(key, clean, sizeof(clean));
    if (!clean[0]) return NULL;
    char fname[144];
    snprintf(fname, sizeof(fname), "%s.img", clean);
    return tt_path_join(cache_dir, fname);
}

uint8_t *thumb_disk_load(const char *cache_dir, const char *key, size_t *out_len) {
    char *p = disk_cache_path(cache_dir, key);
    if (!p) return NULL;
    char *data = tt_read_file(p, out_len);
    free(p);
    return (uint8_t *)data;
}

bool thumb_disk_save(const char *cache_dir, const char *key, const uint8_t *data, size_t len) {
    if (!data || len == 0) return false;
    char *p = disk_cache_path(cache_dir, key);
    if (!p) return false;
    bool ok = tt_write_file_atomic(p, data, len);
    free(p);
    return ok;
}
