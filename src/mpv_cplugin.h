#ifndef TUITUBE_MPV_CPLUGIN_H
#define TUITUBE_MPV_CPLUGIN_H

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define TT_MPV_EXPORT __attribute__((visibility("default")))
#else
#define TT_MPV_EXPORT
#endif

#if defined(__has_include) && __has_include(<mpv/client.h>)
#include <mpv/client.h>
#else

#define MPV_MAKE_VERSION(major, minor) (((major) << 16) | (minor) | 0UL)

typedef struct mpv_handle mpv_handle;

typedef enum mpv_format {
    MPV_FORMAT_NONE       = 0,
    MPV_FORMAT_STRING     = 1,
    MPV_FORMAT_OSD_STRING = 2,
    MPV_FORMAT_FLAG       = 3,
    MPV_FORMAT_INT64      = 4,
    MPV_FORMAT_DOUBLE     = 5,
    MPV_FORMAT_NODE       = 6,
    MPV_FORMAT_NODE_ARRAY = 7,
    MPV_FORMAT_NODE_MAP   = 8,
    MPV_FORMAT_BYTE_ARRAY = 9
} mpv_format;

struct mpv_node_list;
struct mpv_byte_array;

typedef struct mpv_node {
    union {
        char *string;
        int flag;
        int64_t int64;
        double double_;
        struct mpv_node_list *list;
        struct mpv_byte_array *ba;
    } u;
    mpv_format format;
} mpv_node;

typedef struct mpv_node_list {
    int num;
    mpv_node *values;
    char **keys;
} mpv_node_list;

typedef struct mpv_byte_array {
    void *data;
    size_t size;
} mpv_byte_array;

typedef enum mpv_event_id {
    MPV_EVENT_NONE               = 0,
    MPV_EVENT_SHUTDOWN           = 1,
    MPV_EVENT_LOG_MESSAGE        = 2,
    MPV_EVENT_GET_PROPERTY_REPLY = 3,
    MPV_EVENT_SET_PROPERTY_REPLY = 4,
    MPV_EVENT_COMMAND_REPLY      = 5,
    MPV_EVENT_START_FILE         = 6,
    MPV_EVENT_END_FILE           = 7,
    MPV_EVENT_FILE_LOADED        = 8,
    MPV_EVENT_CLIENT_MESSAGE     = 16,
    MPV_EVENT_VIDEO_RECONFIG     = 17,
    MPV_EVENT_AUDIO_RECONFIG     = 18,
    MPV_EVENT_SEEK               = 20,
    MPV_EVENT_PLAYBACK_RESTART   = 21,
    MPV_EVENT_PROPERTY_CHANGE    = 22,
    MPV_EVENT_QUEUE_OVERFLOW     = 24,
    MPV_EVENT_HOOK               = 25
} mpv_event_id;

typedef struct mpv_event_property {
    const char *name;
    mpv_format format;
    void *data;
} mpv_event_property;

typedef struct mpv_event_client_message {
    int num_args;
    const char **args;
} mpv_event_client_message;

typedef struct mpv_event {
    mpv_event_id event_id;
    int error;
    uint64_t reply_userdata;
    void *data;
} mpv_event;

extern unsigned long mpv_client_api_version(void);
extern const char *mpv_error_string(int error);
extern void mpv_free(void *data);
extern const char *mpv_client_name(mpv_handle *ctx);
extern void mpv_free_node_contents(mpv_node *node);
extern int mpv_command(mpv_handle *ctx, const char **args);
extern int mpv_command_node(mpv_handle *ctx, mpv_node *args, mpv_node *result);
extern int mpv_command_ret(mpv_handle *ctx, const char **args, mpv_node *result);
extern int mpv_set_property(mpv_handle *ctx, const char *name, mpv_format format, void *data);
extern int mpv_set_property_string(mpv_handle *ctx, const char *name, const char *data);
extern int mpv_get_property(mpv_handle *ctx, const char *name, mpv_format format, void *data);
extern char *mpv_get_property_string(mpv_handle *ctx, const char *name);
extern int mpv_observe_property(mpv_handle *mpv, uint64_t registered_reply_userdata,
                                const char *name, mpv_format format);
extern int mpv_unobserve_property(mpv_handle *mpv, uint64_t registered_reply_userdata);
extern mpv_event *mpv_wait_event(mpv_handle *ctx, double timeout);
extern void mpv_set_wakeup_callback(mpv_handle *ctx, void (*cb)(void *d), void *d);

#endif

TT_MPV_EXPORT int mpv_open_cplugin(mpv_handle *handle);

#endif
