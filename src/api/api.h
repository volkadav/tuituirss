// SPDX-License-Identifier: MIT
#ifndef TUIIRSS_API_H
#define TUIIRSS_API_H

#include <stdbool.h>
#include <stddef.h>

#include <jansson.h>

#include "config/config.h"
#include "model/model.h"

typedef enum {
    API_OK = 0,
    API_ERR_NETWORK,
    API_ERR_HTTP,
    API_ERR_PARSE,
    API_ERR_STATUS,          /* generic server-side error */
    API_ERR_NOT_LOGGED_IN,
    API_ERR_LOGIN,
    API_ERR_API_DISABLED,
    API_ERR_UNKNOWN_METHOD,
    API_ERR_INCORRECT_USAGE,
    API_ERR_OPERATION_FAILED,
    API_ERR_NOT_FOUND,
} ApiError;

typedef struct ApiClient ApiClient;

ApiClient *api_new(const Config *cfg);
void api_free(ApiClient *c);

ApiError api_last_error(const ApiClient *c);
const char *api_last_error_string(const ApiClient *c);
const char *api_session_id(const ApiClient *c);
void api_set_session(ApiClient *c, const char *sid, int api_level);
/* Retain credentials in memory so an expired session can be renewed without
 * prompting again. Never written to disk. */
void api_set_credentials(ApiClient *c, const char *user, const char *password);
int api_level(const ApiClient *c);
const char *api_server_version(const ApiClient *c);

/* Core request. `params` (may be NULL) is merged into the request object and
 * its reference is consumed. Returns a new json_t reference (the `content`
 * value) on success, or NULL on error. Caller decrefs. */
json_t *api_call(ApiClient *c, const char *op, json_t *params);

/* Map a server `content.error` code to a typed error. Exposed for tests. */
ApiError api_error_from_code(const char *code);

/* Parse a response envelope. On success returns 0 and stores a new reference
 * to `content` in *content_out. On failure returns -1 and fills err_out /
 * errmsg. Exposed for unit testing without a network. */
int api_parse_envelope(const char *json, ApiError *err_out, char *errmsg,
                       size_t errmsg_len, json_t **content_out);

/* Session operations. Return 0 on success, -1 on error. */
int api_login(ApiClient *c, const char *user, const char *password);
int api_logout(ApiClient *c);
int api_is_logged_in(ApiClient *c, bool *logged_in);
int api_get_version(ApiClient *c, char **version_out);

/* Read operations. Return 0 on success, -1 on error. Outputs are owned by
 * the caller and freed with the corresponding *_free(). */
int api_get_categories(ApiClient *c, bool unread_only, bool include_empty,
                       Category **out, size_t *count);
int api_get_feeds(ApiClient *c, int cat_id, bool unread_only,
                  bool include_nested, Feed **out, size_t *count);
int api_get_headlines(ApiClient *c, int feed_id, bool is_cat, int limit,
                      int skip, const char *view_mode, const char *search,
                      bool show_content, bool include_attachments,
                      Headline **out, size_t *count);
int api_get_article(ApiClient *c, int article_id, Headline **out,
                    size_t *count);
int api_get_counters(ApiClient *c, Counter *out);
int api_get_labels(ApiClient *c, int article_id, Label **out, size_t *count);
int api_get_config(ApiClient *c, int *num_feeds);

/* Mutations. Return 0 on success, -1 on error. */
int api_update_article(ApiClient *c, const int *ids, size_t n, int mode,
                       int field, const char *data);
int api_catchup_feed(ApiClient *c, int feed_id, bool is_cat, const char *mode);
int api_set_article_label(ApiClient *c, const int *ids, size_t n,
                          int label_id, bool assign);
int api_subscribe_feed(ApiClient *c, const char *url, int cat_id,
                       const char *login, const char *password);
int api_unsubscribe_feed(ApiClient *c, int feed_id);
int api_update_feed(ApiClient *c, int feed_id);

#endif /* TUIIRSS_API_H */
