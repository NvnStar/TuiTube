#include "http.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HTTP_USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
#define HTTP_JOB_QUEUE_CAP 256

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    size_t max_bytes;
    bool overflow;
} CurlWriteCtx;

typedef struct {
    HttpJobType type;
    uint64_t request_gen;
    void *arg;
    HttpWorkFn work_fn;
    HttpFreeFn free_arg;
    HttpFreeFn free_res;
} QueuedJob;

typedef struct ResultNode {
    HttpResult res;
    struct ResultNode *next;
} ResultNode;

struct HttpPool {
    pthread_t *threads;
    int num_threads;

    pthread_mutex_t mu;
    pthread_cond_t cond;
    bool stop;
    uint64_t active_gen;
    uint64_t active_detail_gen;

    QueuedJob jobs[HTTP_JOB_QUEUE_CAP];
    size_t job_count;

    ResultNode *res_head;
    ResultNode *res_tail;

    int notify_fds[2];
};

bool http_global_init(void) {
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void http_global_cleanup(void) {
    curl_global_cleanup();
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    CurlWriteCtx *ctx = userdata;
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    size_t total = size * nmemb;
    if (ctx->len + total > ctx->max_bytes) {
        ctx->overflow = true;
        return 0;
    }
    if (ctx->len + total + 1 > ctx->cap) {
        size_t new_cap = ctx->cap ? ctx->cap * 2 : 16384;
        while (new_cap < ctx->len + total + 1) {
            if (new_cap > ctx->max_bytes + 1) {
                new_cap = ctx->max_bytes + 1;
                break;
            }
            new_cap *= 2;
        }
        char *nb = realloc(ctx->buf, new_cap);
        if (!nb) return 0;
        ctx->buf = nb;
        ctx->cap = new_cap;
    }
    memcpy(ctx->buf + ctx->len, ptr, total);
    ctx->len += total;
    ctx->buf[ctx->len] = '\0';
    return total;
}

static bool perform_curl(CURL *reuse_handle, const char *url,
                         const char *post_body,
                         const char *const *headers, size_t header_count,
                         size_t max_bytes, HttpResponse *out) {
    memset(out, 0, sizeof(*out));
    if (!url || !url[0]) {
        snprintf(out->error, sizeof(out->error), "Empty URL");
        return false;
    }

    CURL *curl = reuse_handle ? reuse_handle : curl_easy_init();
    if (!curl) {
        snprintf(out->error, sizeof(out->error), "Failed to init CURL");
        return false;
    }
    if (reuse_handle) {
        curl_easy_reset(curl);
    }

    CurlWriteCtx ctx = {0};
    ctx.max_bytes = max_bytes ? max_bytes : (16u * 1024u * 1024u);

    struct curl_slist *slist = NULL;
    for (size_t i = 0; i < header_count; i++) {
        if (headers[i]) {
            slist = curl_slist_append(slist, headers[i]);
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    if (slist) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    }
    if (post_body) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(post_body));
    }

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status_code);

    if (slist) curl_slist_free_all(slist);
    if (reuse_handle) {
        curl_easy_reset(curl);
    } else {
        curl_easy_cleanup(curl);
    }

    if (rc != CURLE_OK) {
        if (ctx.overflow) {
            snprintf(out->error, sizeof(out->error), "Response exceeded size limit");
        } else {
            snprintf(out->error, sizeof(out->error), "%s", curl_easy_strerror(rc));
        }
        free(ctx.buf);
        return false;
    }

    if (!ctx.buf) {
        ctx.buf = tt_strdup("");
        ctx.len = 0;
    }
    out->data = ctx.buf;
    out->len = ctx.len;
    return true;
}

bool http_get(CURL *reuse_handle, const char *url, size_t max_bytes, HttpResponse *out) {
    return perform_curl(reuse_handle, url, NULL, NULL, 0, max_bytes, out);
}

bool http_post_json(CURL *reuse_handle, const char *url, const char *json_body,
                    const char *const *headers, size_t header_count,
                    size_t max_bytes, HttpResponse *out) {
    return perform_curl(reuse_handle, url, json_body ? json_body : "{}",
                        headers, header_count, max_bytes, out);
}

void http_response_free(HttpResponse *res) {
    if (!res) return;
    free(res->data);
    memset(res, 0, sizeof(*res));
}

static bool set_nonblock_cloexec(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int fdfl = fcntl(fd, F_GETFD, 0);
    if (fdfl >= 0) fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC);
    return true;
}

static void push_result_locked(HttpPool *pool, HttpResult res) {
    ResultNode *node = malloc(sizeof(ResultNode));
    if (!node) {
        http_result_free(&res);
        return;
    }
    node->res = res;
    node->next = NULL;
    if (pool->res_tail) {
        pool->res_tail->next = node;
    } else {
        pool->res_head = node;
    }
    pool->res_tail = node;

    char byte = 1;
    ssize_t wr = write(pool->notify_fds[1], &byte, 1);
    (void)wr;
}

static void *worker_main(void *arg) {
    HttpPool *pool = arg;
    CURL *curl = curl_easy_init();

    for (;;) {
        pthread_mutex_lock(&pool->mu);
        while (!pool->stop && pool->job_count == 0) {
            pthread_cond_wait(&pool->cond, &pool->mu);
        }
        if (pool->stop) {
            pthread_mutex_unlock(&pool->mu);
            break;
        }

        /* Priority: pick earliest non-thumbnail job first, else earliest job */
        size_t pick = 0;
        bool found_interactive = false;
        for (size_t i = 0; i < pool->job_count; i++) {
            if (pool->jobs[i].type != JOB_THUMBNAIL) {
                pick = i;
                found_interactive = true;
                break;
            }
        }
        (void)found_interactive;

        QueuedJob job = pool->jobs[pick];
        if (pick + 1 < pool->job_count) {
            memmove(&pool->jobs[pick], &pool->jobs[pick + 1],
                    (pool->job_count - pick - 1) * sizeof(QueuedJob));
        }
        pool->job_count--;
        uint64_t active_gen = pool->active_gen;
        uint64_t active_detail_gen = pool->active_detail_gen;
        pthread_mutex_unlock(&pool->mu);

        /* Skip already-superseded jobs */
        bool stale = false;
        if (job.type == JOB_DETAIL) {
            stale = (job.request_gen < active_detail_gen);
        } else if (job.type != JOB_THUMBNAIL) {
            stale = (job.request_gen < active_gen);
        }
        if (stale) {
            if (job.free_arg) job.free_arg(job.arg);
            continue;
        }

        void *out_data = NULL;
        if (job.work_fn) {
            out_data = job.work_fn(curl, job.arg, job.request_gen, pool);
        }
        if (job.free_arg) job.free_arg(job.arg);

        pthread_mutex_lock(&pool->mu);
        if (!pool->stop) {
            HttpResult res = {0};
            res.kind = RES_JOB_DONE;
            res.job_type = job.type;
            res.request_gen = job.request_gen;
            res.data = out_data;
            res.free_data = job.free_res;
            push_result_locked(pool, res);
        } else if (out_data && job.free_res) {
            job.free_res(out_data);
        }
        pthread_mutex_unlock(&pool->mu);
    }

    if (curl) curl_easy_cleanup(curl);
    return NULL;
}

HttpPool *http_pool_create(int num_threads) {
    if (num_threads <= 0) num_threads = 4;
    HttpPool *pool = calloc(1, sizeof(HttpPool));
    if (!pool) return NULL;

    if (pipe(pool->notify_fds) != 0) {
        free(pool);
        return NULL;
    }
    set_nonblock_cloexec(pool->notify_fds[0]);
    set_nonblock_cloexec(pool->notify_fds[1]);

    pthread_mutex_init(&pool->mu, NULL);
    pthread_cond_init(&pool->cond, NULL);

    pool->threads = calloc((size_t)num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        close(pool->notify_fds[0]);
        close(pool->notify_fds[1]);
        pthread_mutex_destroy(&pool->mu);
        pthread_cond_destroy(&pool->cond);
        free(pool);
        return NULL;
    }

    pool->num_threads = 0;
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_main, pool) == 0) {
            pool->num_threads++;
        }
    }
    return pool;
}

void http_pool_destroy(HttpPool *pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mu);
    pool->stop = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mu);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    for (size_t i = 0; i < pool->job_count; i++) {
        if (pool->jobs[i].free_arg) pool->jobs[i].free_arg(pool->jobs[i].arg);
    }
    ResultNode *n = pool->res_head;
    while (n) {
        ResultNode *next = n->next;
        http_result_free(&n->res);
        free(n);
        n = next;
    }

    close(pool->notify_fds[0]);
    close(pool->notify_fds[1]);
    pthread_mutex_destroy(&pool->mu);
    pthread_cond_destroy(&pool->cond);
    free(pool->threads);
    free(pool);
}

int http_pool_notify_fd(const HttpPool *pool) {
    return pool ? pool->notify_fds[0] : -1;
}

void http_pool_drain_notify(HttpPool *pool) {
    if (!pool) return;
    char buf[128];
    while (read(pool->notify_fds[0], buf, sizeof(buf)) > 0) {
    }
}

bool http_pool_submit(HttpPool *pool, HttpJobType type, uint64_t request_gen,
                      void *arg, HttpWorkFn work_fn,
                      HttpFreeFn free_arg, HttpFreeFn free_res) {
    if (!pool || !work_fn) {
        if (free_arg && arg) free_arg(arg);
        return false;
    }

    pthread_mutex_lock(&pool->mu);
    if (pool->stop) {
        pthread_mutex_unlock(&pool->mu);
        if (free_arg && arg) free_arg(arg);
        return false;
    }

    if (pool->job_count >= HTTP_JOB_QUEUE_CAP) {
        /* Evict oldest thumbnail job to make room */
        bool evicted = false;
        for (size_t i = 0; i < pool->job_count; i++) {
            if (pool->jobs[i].type == JOB_THUMBNAIL) {
                if (pool->jobs[i].free_arg) pool->jobs[i].free_arg(pool->jobs[i].arg);
                if (i + 1 < pool->job_count) {
                    memmove(&pool->jobs[i], &pool->jobs[i + 1],
                            (pool->job_count - i - 1) * sizeof(QueuedJob));
                }
                pool->job_count--;
                evicted = true;
                break;
            }
        }
        if (!evicted) {
            for (size_t i = 0; i < pool->job_count; i++) {
                if (pool->jobs[i].type == JOB_DETAIL &&
                    pool->jobs[i].request_gen < pool->active_detail_gen) {
                    if (pool->jobs[i].free_arg) pool->jobs[i].free_arg(pool->jobs[i].arg);
                    if (i + 1 < pool->job_count) {
                        memmove(&pool->jobs[i], &pool->jobs[i + 1],
                                (pool->job_count - i - 1) * sizeof(QueuedJob));
                    }
                    pool->job_count--;
                    evicted = true;
                    break;
                }
            }
        }
        if (!evicted) {
            pthread_mutex_unlock(&pool->mu);
            if (free_arg && arg) free_arg(arg);
            return false;
        }
    }

    pool->jobs[pool->job_count++] = (QueuedJob){
        .type = type,
        .request_gen = request_gen,
        .arg = arg,
        .work_fn = work_fn,
        .free_arg = free_arg,
        .free_res = free_res,
    };
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mu);
    return true;
}

void http_pool_set_active_gen(HttpPool *pool, uint64_t gen) {
    if (!pool) return;
    pthread_mutex_lock(&pool->mu);
    pool->active_gen = gen;
    pthread_mutex_unlock(&pool->mu);
}

void http_pool_drop_thumbnails(HttpPool *pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->mu);
    size_t w = 0;
    for (size_t i = 0; i < pool->job_count; i++) {
        if (pool->jobs[i].type == JOB_THUMBNAIL) {
            if (pool->jobs[i].free_arg) pool->jobs[i].free_arg(pool->jobs[i].arg);
        } else {
            if (w != i) pool->jobs[w] = pool->jobs[i];
            w++;
        }
    }
    pool->job_count = w;
    pthread_mutex_unlock(&pool->mu);
}

void http_pool_set_active_detail_gen(HttpPool *pool, uint64_t gen) {
    if (!pool) return;
    pthread_mutex_lock(&pool->mu);
    pool->active_detail_gen = gen;
    pthread_mutex_unlock(&pool->mu);
}

bool http_pool_should_cancel(HttpPool *pool, uint64_t request_gen) {
    if (!pool) return false;
    pthread_mutex_lock(&pool->mu);
    bool cancel = pool->stop || (request_gen < pool->active_gen);
    pthread_mutex_unlock(&pool->mu);
    return cancel;
}

void http_pool_post_progress(HttpPool *pool, HttpJobType type, uint64_t gen, const char *msg) {
    if (!pool || !msg) return;
    pthread_mutex_lock(&pool->mu);
    if (!pool->stop) {
        HttpResult res = {0};
        res.kind = RES_PROGRESS;
        res.job_type = type;
        res.request_gen = gen;
        res.progress_text = tt_strdup(msg);
        push_result_locked(pool, res);
    }
    pthread_mutex_unlock(&pool->mu);
}

bool http_pool_pop_result(HttpPool *pool, HttpResult *out) {
    if (!pool || !out) return false;
    pthread_mutex_lock(&pool->mu);
    ResultNode *node = pool->res_head;
    if (!node) {
        pthread_mutex_unlock(&pool->mu);
        return false;
    }
    pool->res_head = node->next;
    if (!pool->res_head) pool->res_tail = NULL;
    pthread_mutex_unlock(&pool->mu);

    *out = node->res;
    free(node);
    return true;
}

void http_result_free(HttpResult *res) {
    if (!res) return;
    if (res->data && res->free_data) {
        res->free_data(res->data);
    }
    free(res->progress_text);
    memset(res, 0, sizeof(*res));
}
