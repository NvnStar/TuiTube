#ifndef TUITUBE_HTTP_H
#define TUITUBE_HTTP_H

#include <curl/curl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    long status_code;
    char *data;
    size_t len;
    char error[256];
} HttpResponse;

typedef enum {
    JOB_SEARCH = 0,
    JOB_BROWSE,
    JOB_NEXT,
    JOB_DETAIL,
    JOB_FEED,
    JOB_RESOLVE_URL,
    JOB_THUMBNAIL
} HttpJobType;

typedef enum {
    RES_JOB_DONE = 0,
    RES_PROGRESS = 1
} HttpResultKind;

typedef struct HttpPool HttpPool;

typedef void *(*HttpWorkFn)(CURL *curl, void *arg, uint64_t gen, HttpPool *pool);
typedef void (*HttpFreeFn)(void *ptr);

typedef struct {
    HttpResultKind kind;
    HttpJobType job_type;
    uint64_t request_gen;
    void *data;
    HttpFreeFn free_data;
    char *progress_text;
} HttpResult;

bool http_global_init(void);
void http_global_cleanup(void);

bool http_get(CURL *reuse_handle, const char *url, size_t max_bytes, HttpResponse *out);
bool http_post_json(CURL *reuse_handle, const char *url, const char *json_body,
                    const char *const *headers, size_t header_count,
                    size_t max_bytes, HttpResponse *out);
void http_response_free(HttpResponse *res);

HttpPool *http_pool_create(int num_threads);
void http_pool_destroy(HttpPool *pool);
int http_pool_notify_fd(const HttpPool *pool);
void http_pool_drain_notify(HttpPool *pool);

bool http_pool_submit(HttpPool *pool, HttpJobType type, uint64_t request_gen,
                      void *arg, HttpWorkFn work_fn,
                      HttpFreeFn free_arg, HttpFreeFn free_res);
bool http_pool_should_cancel(HttpPool *pool, uint64_t request_gen);
void http_pool_drop_thumbnails(HttpPool *pool);
void http_pool_set_active_gen(HttpPool *pool, uint64_t gen);
void http_pool_set_active_detail_gen(HttpPool *pool, uint64_t gen);
void http_pool_post_progress(HttpPool *pool, HttpJobType type, uint64_t gen, const char *msg);
bool http_pool_pop_result(HttpPool *pool, HttpResult *out);
void http_result_free(HttpResult *res);

#endif
