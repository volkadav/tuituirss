// SPDX-License-Identifier: MIT
#include "api/api.h"
#include "util/util.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

/* Upper bound on a single API response. Protects against hostile servers,
 * unbounded/chunked bodies, and decompression bombs (the write callback sees
 * already-decompressed bytes). */
#define API_MAX_RESPONSE_BYTES (16u * 1024u * 1024u)

struct ApiClient {
    const Config *cfg;
    CURL *curl;
    char *session_id;
    char *username;
    char *password;
    int level;
    char *server_version;
    char errbuf[CURL_ERROR_SIZE];
    char error[512];
    ApiError last_error;

    char *response;
    size_t response_len;
    size_t response_cap;
    bool too_large;

    struct curl_slist *headers;
};

/* ----------------------------------------------------------------------- */
/* helpers                                                                 */
/* ----------------------------------------------------------------------- */

static void set_error(ApiClient *c, ApiError err, const char *fmt, ...)
{
    c->last_error = err;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->error, sizeof c->error, fmt, ap);
    va_end(ap);
    log_error("api: %s", c->error);
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ApiClient *c = userdata;
    size_t n = size * nmemb;

    if (n > API_MAX_RESPONSE_BYTES ||
        c->response_len > API_MAX_RESPONSE_BYTES - n) {
        c->too_large = true;
        return 0; /* abort the transfer */
    }
    if (c->response_len + n + 1 > c->response_cap) {
        size_t cap = c->response_cap ? c->response_cap : 4096;
        while (cap < c->response_len + n + 1)
            cap *= 2;
        c->response = xrealloc(c->response, cap);
        c->response_cap = cap;
    }
    memcpy(c->response + c->response_len, ptr, n);
    c->response_len += n;
    c->response[c->response_len] = '\0';
    return n;
}

ApiClient *api_new(const Config *cfg)
{
    ApiClient *c = xcalloc(1, sizeof *c);
    c->cfg = cfg;
    c->level = 0;
    c->last_error = API_OK;
    c->curl = curl_easy_init();
    if (!c->curl) {
        set_error(c, API_ERR_NETWORK, "failed to initialise libcurl");
        free(c);
        return NULL;
    }
    c->headers = curl_slist_append(c->headers, "Content-Type: application/json");
    c->headers = curl_slist_append(c->headers, "Accept: application/json");
    c->errbuf[0] = '\0';
    return c;
}

void api_free(ApiClient *c)
{
    if (!c)
        return;
    if (c->curl)
        curl_easy_cleanup(c->curl);
    if (c->headers)
        curl_slist_free_all(c->headers);
    free(c->session_id);
    free(c->server_version);
    free(c->response);
    if (c->password) {
        memset(c->password, 0, strlen(c->password));
        free(c->password);
    }
    free(c->username);
    free(c);
}

ApiError api_last_error(const ApiClient *c)
{
    return c ? c->last_error : API_ERR_NETWORK;
}

const char *api_last_error_string(const ApiClient *c)
{
    if (!c)
        return "no client";
    return c->error[0] ? c->error : "unknown error";
}

const char *api_session_id(const ApiClient *c)
{
    return c ? c->session_id : NULL;
}

void api_set_session(ApiClient *c, const char *sid, int api_level)
{
    if (!c)
        return;
    free(c->session_id);
    c->session_id = sid ? xstrdup(sid) : NULL;
    c->level = api_level;
}

void api_set_credentials(ApiClient *c, const char *user, const char *password)
{
    if (!c)
        return;
    free(c->username);
    c->username = user ? xstrdup(user) : NULL;
    if (c->password) {
        memset(c->password, 0, strlen(c->password));
        free(c->password);
    }
    c->password = password ? xstrdup(password) : NULL;
}

int api_level(const ApiClient *c)
{
    return c ? c->level : 0;
}

const char *api_server_version(const ApiClient *c)
{
    return c ? c->server_version : NULL;
}

/* ----------------------------------------------------------------------- */
/* envelope / error mapping                                                */
/* ----------------------------------------------------------------------- */

ApiError api_error_from_code(const char *code)
{
    if (!code)
        return API_ERR_STATUS;
    if (strcmp(code, "NOT_LOGGED_IN") == 0)
        return API_ERR_NOT_LOGGED_IN;
    if (strcmp(code, "LOGIN_ERROR") == 0)
        return API_ERR_LOGIN;
    if (strcmp(code, "API_DISABLED") == 0)
        return API_ERR_API_DISABLED;
    if (strcmp(code, "UNKNOWN_METHOD") == 0)
        return API_ERR_UNKNOWN_METHOD;
    if (strcmp(code, "INCORRECT_USAGE") == 0)
        return API_ERR_INCORRECT_USAGE;
    if (strcmp(code, "E_OPERATION_FAILED") == 0)
        return API_ERR_OPERATION_FAILED;
    if (strcmp(code, "E_NOT_FOUND") == 0)
        return API_ERR_NOT_FOUND;
    return API_ERR_STATUS;
}

int api_parse_envelope(const char *json, ApiError *err_out, char *errmsg,
                       size_t errmsg_len, json_t **content_out)
{
    if (content_out)
        *content_out = NULL;
    if (err_out)
        *err_out = API_OK;

    json_error_t jerr;
    json_t *root = json_loads(json ? json : "", 0, &jerr);
    if (!root) {
        if (err_out)
            *err_out = API_ERR_PARSE;
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "invalid JSON response: %s", jerr.text);
        return -1;
    }
    if (!json_is_object(root)) {
        if (err_out)
            *err_out = API_ERR_PARSE;
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "response is not a JSON object");
        json_decref(root);
        return -1;
    }

    json_t *status = json_object_get(root, "status");
    if (!json_is_integer(status) || json_integer_value(status) != 0) {
        json_t *content = json_object_get(root, "content");
        const char *code = NULL;
        if (json_is_object(content))
            code = json_string_value(json_object_get(content, "error"));
        if (err_out)
            *err_out = api_error_from_code(code);
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "server error: %s",
                     code ? code : "unknown");
        json_decref(root);
        return -1;
    }

    json_t *content = json_object_get(root, "content");
    if (content)
        json_incref(content);
    else
        content = json_object();
    json_decref(root);
    if (content_out)
        *content_out = content;
    else
        json_decref(content);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* core call                                                               */
/* ----------------------------------------------------------------------- */

static json_t *api_call_once(ApiClient *c, const char *op, json_t *params)
{
    if (!c || !c->curl)
        return NULL;
    c->last_error = API_OK;
    c->error[0] = '\0';
    c->response_len = 0;
    c->too_large = false;
    if (c->response)
        c->response[0] = '\0';

    json_t *req = json_object();
    json_object_set_new(req, "op", json_string(op));
    if (params)
        json_object_update(req, params);
    if (c->session_id && strcmp(op, "login") != 0)
        json_object_set_new(req, "sid", json_string(c->session_id));

    autofree char *body = json_dumps(req, JSON_COMPACT);
    json_decref(req);
    if (!body) {
        set_error(c, API_ERR_PARSE, "failed to serialise request");
        return NULL;
    }
    autofree char *redacted = log_redact_json(body);
    log_debug("POST %s %s", c->cfg->server_url, redacted);

    curl_easy_setopt(c->curl, CURLOPT_URL, c->cfg->server_url);
    curl_easy_setopt(c->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(c->curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c->curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    curl_easy_setopt(c->curl, CURLOPT_HTTPHEADER, c->headers);
    curl_easy_setopt(c->curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c->curl, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->curl, CURLOPT_TIMEOUT, (long)c->cfg->timeout_sec);
    /* The API endpoint is fixed, so never follow redirects: on 307/308 libcurl
     * would replay the POST body (which can contain the password) to whatever
     * host the redirect names, and redirects also permit protocol downgrade. */
    curl_easy_setopt(c->curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c->curl, CURLOPT_MAXREDIRS, 0L);
    /* Restrict the transport to the configured scheme. */
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c->curl, CURLOPT_PROTOCOLS_STR,
                     strncmp(c->cfg->server_url, "https:", 6) == 0
                         ? "https"
                         : "http,https");
#else
    {
        long proto = CURLPROTO_HTTPS;
        if (strncmp(c->cfg->server_url, "https:", 6) != 0)
            proto |= CURLPROTO_HTTP;
        curl_easy_setopt(c->curl, CURLOPT_PROTOCOLS, proto);
    }
#endif
    curl_easy_setopt(c->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c->curl, CURLOPT_USERAGENT, "tuituirss/0.1");
    curl_easy_setopt(c->curl, CURLOPT_ERRORBUFFER, c->errbuf);
    if (c->cfg->insecure) {
        curl_easy_setopt(c->curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c->curl, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
        curl_easy_setopt(c->curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c->curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    if (c->cfg->ca_file && *c->cfg->ca_file)
        curl_easy_setopt(c->curl, CURLOPT_CAINFO, c->cfg->ca_file);

    CURLcode rc = curl_easy_perform(c->curl);
    if (c->too_large) {
        set_error(c, API_ERR_HTTP, "response exceeded %u bytes",
                  (unsigned)API_MAX_RESPONSE_BYTES);
        return NULL;
    }
    if (rc != CURLE_OK) {
        set_error(c, API_ERR_NETWORK, "network error: %s",
                  c->errbuf[0] ? c->errbuf : curl_easy_strerror(rc));
        return NULL;
    }

    long http_code = 0;
    curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 300 && http_code < 400) {
        set_error(c, API_ERR_HTTP,
                  "server returned redirect HTTP %ld; set server_url to the "
                  "final endpoint",
                  http_code);
        return NULL;
    }
    if (http_code < 200 || http_code >= 300) {
        set_error(c, API_ERR_HTTP, "server returned HTTP %ld", http_code);
        return NULL;
    }

    json_t *content = NULL;
    ApiError err = API_OK;
    if (api_parse_envelope(c->response, &err, c->error, sizeof c->error,
                           &content) != 0) {
        c->last_error = err;
        log_error("api: %s", c->error);
        return NULL;
    }
    return content;
}

json_t *api_call(ApiClient *c, const char *op, json_t *params)
{
    json_t *content = api_call_once(c, op, params);
    if (!content && c && api_last_error(c) == API_ERR_NOT_LOGGED_IN &&
        strcmp(op, "login") != 0 && c->username && c->password) {
        /* Session expired: renew it once and replay the request. */
        if (api_login(c, c->username, c->password) == 0)
            content = api_call_once(c, op, params);
    }
    if (params)
        json_decref(params);
    return content;
}

/* ----------------------------------------------------------------------- */
/* session operations                                                      */
/* ----------------------------------------------------------------------- */

int api_login(ApiClient *c, const char *user, const char *password)
{
    json_t *p = json_object();
    json_object_set_new(p, "user", json_string(user));
    json_object_set_new(p, "password", json_string(password));
    json_t *content = api_call(c, "login", p);
    if (!content)
        return -1;

    const char *sid = json_string_value(json_object_get(content, "session_id"));
    if (!sid) {
        set_error(c, API_ERR_PARSE, "login response missing session_id");
        json_decref(content);
        return -1;
    }
    int level = (int)json_integer_value(json_object_get(content, "api_level"));
    free(c->session_id);
    c->session_id = xstrdup(sid);
    c->level = level;
    json_decref(content);
    return 0;
}

int api_logout(ApiClient *c)
{
    json_t *content = api_call(c, "logout", NULL);
    if (content)
        json_decref(content);
    free(c->session_id);
    c->session_id = NULL;
    return content ? 0 : -1;
}

int api_is_logged_in(ApiClient *c, bool *logged_in)
{
    if (logged_in)
        *logged_in = false;
    json_t *content = api_call(c, "isLoggedIn", NULL);
    if (!content)
        return -1;
    json_t *v = json_object_get(content, "status");
    bool ok = false;
    if (json_is_boolean(v))
        ok = json_is_true(v);
    else if (json_is_integer(v))
        ok = json_integer_value(v) != 0;
    if (logged_in)
        *logged_in = ok;
    json_decref(content);
    return 0;
}

int api_get_version(ApiClient *c, char **version_out)
{
    json_t *content = api_call(c, "getVersion", NULL);
    if (!content)
        return -1;
    const char *v = json_string_value(json_object_get(content, "version"));
    free(c->server_version);
    c->server_version = v ? xstrdup(v) : NULL;
    if (version_out)
        *version_out = c->server_version ? xstrdup(c->server_version) : NULL;
    json_decref(content);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* read operations                                                         */
/* ----------------------------------------------------------------------- */

int api_get_categories(ApiClient *c, bool unread_only, bool include_empty,
                       Category **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    json_t *p = json_object();
    json_object_set_new(p, "unread_only", json_boolean(unread_only));
    json_object_set_new(p, "enable_nested", json_true());
    json_object_set_new(p, "include_empty", json_boolean(include_empty));
    json_t *content = api_call(c, "getCategories", p);
    if (!content)
        return -1;
    *out = parse_categories(content, count);
    json_decref(content);
    return 0;
}

int api_get_feeds(ApiClient *c, int cat_id, bool unread_only,
                  bool include_nested, Feed **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    json_t *p = json_object();
    json_object_set_new(p, "cat_id", json_integer(cat_id));
    json_object_set_new(p, "unread_only", json_boolean(unread_only));
    json_object_set_new(p, "include_nested", json_boolean(include_nested));
    json_t *content = api_call(c, "getFeeds", p);
    if (!content)
        return -1;
    *out = parse_feeds(content, count);
    json_decref(content);
    return 0;
}

int api_get_headlines(ApiClient *c, int feed_id, bool is_cat, int limit,
                      int skip, const char *view_mode, const char *search,
                      bool show_content, bool include_attachments,
                      Headline **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    json_t *p = json_object();
    json_object_set_new(p, "feed_id", json_integer(feed_id));
    json_object_set_new(p, "is_cat", json_boolean(is_cat));
    json_object_set_new(p, "limit", json_integer(limit));
    json_object_set_new(p, "skip", json_integer(skip));
    json_object_set_new(p, "view_mode",
                        json_string(view_mode ? view_mode : VIEW_ALL_ARTICLES));
    json_object_set_new(p, "show_excerpt", json_true());
    json_object_set_new(p, "show_content", json_boolean(show_content));
    json_object_set_new(p, "include_attachments",
                        json_boolean(include_attachments));
    if (search && *search)
        json_object_set_new(p, "search", json_string(search));
    json_t *content = api_call(c, "getHeadlines", p);
    if (!content)
        return -1;
    *out = parse_headlines(content, count);
    json_decref(content);
    return 0;
}

int api_get_article(ApiClient *c, int article_id, Headline **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    json_t *p = json_object();
    json_object_set_new(p, "article_id", json_integer(article_id));
    json_t *content = api_call(c, "getArticle", p);
    if (!content)
        return -1;
    *out = parse_article(content, count);
    json_decref(content);
    return 0;
}

int api_get_counters(ApiClient *c, Counter *out)
{
    json_t *content = api_call(c, "getCounters", NULL);
    if (!content)
        return -1;
    *out = parse_counters(content);
    json_decref(content);
    return 0;
}

int api_get_labels(ApiClient *c, int article_id, Label **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    json_t *p = json_object();
    if (article_id > 0)
        json_object_set_new(p, "article_id", json_integer(article_id));
    json_t *content = api_call(c, "getLabels", p);
    if (!content)
        return -1;
    *out = parse_labels(content, count);
    json_decref(content);
    return 0;
}

int api_get_config(ApiClient *c, int *num_feeds)
{
    json_t *content = api_call(c, "getConfig", NULL);
    if (!content)
        return -1;
    if (num_feeds) {
        json_t *v = json_object_get(content, "num_feeds");
        *num_feeds = json_is_integer(v) ? (int)json_integer_value(v) : 0;
    }
    json_decref(content);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* mutations                                                               */
/* ----------------------------------------------------------------------- */

static json_t *int_array(const int *ids, size_t n)
{
    json_t *arr = json_array();
    for (size_t i = 0; i < n; i++)
        json_array_append_new(arr, json_integer(ids[i]));
    return arr;
}

int api_update_article(ApiClient *c, const int *ids, size_t n, int mode,
                       int field, const char *data)
{
    json_t *p = json_object();
    json_object_set_new(p, "article_ids", int_array(ids, n));
    json_object_set_new(p, "mode", json_integer(mode));
    json_object_set_new(p, "field", json_integer(field));
    if (data)
        json_object_set_new(p, "data", json_string(data));
    json_t *content = api_call(c, "updateArticle", p);
    if (!content)
        return -1;
    json_decref(content);
    return 0;
}

int api_catchup_feed(ApiClient *c, int feed_id, bool is_cat, const char *mode)
{
    json_t *p = json_object();
    json_object_set_new(p, "feed_id", json_integer(feed_id));
    json_object_set_new(p, "is_cat", json_boolean(is_cat));
    json_object_set_new(p, "mode", json_string(mode ? mode : "all"));
    json_t *content = api_call(c, "catchupFeed", p);
    if (!content)
        return -1;
    json_decref(content);
    return 0;
}

int api_set_article_label(ApiClient *c, const int *ids, size_t n,
                          int label_id, bool assign)
{
    json_t *p = json_object();
    json_object_set_new(p, "article_ids", int_array(ids, n));
    json_object_set_new(p, "label_id", json_integer(label_id));
    json_object_set_new(p, "assign", json_boolean(assign));
    json_t *content = api_call(c, "setArticleLabel", p);
    if (!content)
        return -1;
    json_decref(content);
    return 0;
}

int api_subscribe_feed(ApiClient *c, const char *url, int cat_id,
                       const char *login, const char *password)
{
    json_t *p = json_object();
    json_object_set_new(p, "feed_url", json_string(url));
    json_object_set_new(p, "category_id", json_integer(cat_id));
    if (login)
        json_object_set_new(p, "login", json_string(login));
    if (password)
        json_object_set_new(p, "password", json_string(password));
    json_t *content = api_call(c, "subscribeToFeed", p);
    if (!content)
        return -1;
    json_decref(content);
    return 0;
}

int api_unsubscribe_feed(ApiClient *c, int feed_id)
{
    json_t *p = json_object();
    json_object_set_new(p, "feed_id", json_integer(feed_id));
    json_t *content = api_call(c, "unsubscribeFeed", p);
    if (!content)
        return -1;
    json_decref(content);
    return 0;
}

int api_update_feed(ApiClient *c, int feed_id)
{
    json_t *p = json_object();
    json_object_set_new(p, "feed_id", json_integer(feed_id));
    json_t *content = api_call(c, "updateFeed", p);
    if (!content)
        return -1;
    json_decref(content);
    return 0;
}
