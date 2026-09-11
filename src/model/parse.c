// SPDX-License-Identifier: MIT
#include "model/model.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* small extraction helpers                                                */
/* ----------------------------------------------------------------------- */

static char *js_str(json_t *obj, const char *key)
{
    if (!obj)
        return NULL;
    json_t *v = json_object_get(obj, key);
    if (!v || json_is_null(v))
        return NULL;
    if (json_is_string(v)) {
        const char *s = json_string_value(v);
        return s ? str_sanitize_copy(s) : NULL;
    }
    /* Some deployments stringify scalars. */
    if (json_is_integer(v))
        return xasprintf("%lld", (long long)json_integer_value(v));
    if (json_is_real(v))
        return xasprintf("%g", json_real_value(v));
    return NULL;
}

static int js_int(json_t *obj, const char *key, int def)
{
    if (!obj)
        return def;
    json_t *v = json_object_get(obj, key);
    if (!v || json_is_null(v))
        return def;
    if (json_is_integer(v))
        return (int)json_integer_value(v);
    if (json_is_real(v))
        return (int)json_real_value(v);
    if (json_is_string(v))
        return (int)strtol(json_string_value(v), NULL, 10);
    return def;
}

/* Like js_str but decodes HTML entities (titles/author/feed names). */
static char *js_str_decoded(json_t *obj, const char *key)
{
    char *raw = js_str(obj, key);
    if (!raw)
        return NULL;
    char *dec = html_entity_decode(raw);
    free(raw);
    return dec;
}

static time_t js_time(json_t *obj, const char *key)
{
    if (!obj)
        return 0;
    json_t *v = json_object_get(obj, key);
    if (!v || json_is_null(v))
        return 0;
    if (json_is_integer(v))
        return (time_t)json_integer_value(v);
    if (json_is_real(v))
        return (time_t)json_real_value(v);
    if (json_is_string(v))
        return (time_t)strtoll(json_string_value(v), NULL, 10);
    return 0;
}

static bool js_bool(json_t *obj, const char *key, bool def)
{
    if (!obj)
        return def;
    json_t *v = json_object_get(obj, key);
    if (!v || json_is_null(v))
        return def;
    if (json_is_boolean(v))
        return json_is_true(v);
    if (json_is_integer(v))
        return json_integer_value(v) != 0;
    if (json_is_string(v)) {
        const char *s = json_string_value(v);
        return s && (s[0] == 't' || s[0] == '1' || s[0] == 'T');
    }
    return def;
}

/* Parse a label entry that may be an object (getLabels) or a positional array
 * [feed_id, caption, fg, bg] (getHeadlines/getArticle). */
static bool parse_label_entry(json_t *v, Label *out)
{
    memset(out, 0, sizeof *out);
    if (json_is_object(v)) {
        out->id = js_int(v, "id", 0);
        out->caption = js_str(v, "caption");
        out->fg_color = js_str(v, "fg_color");
        out->bg_color = js_str(v, "bg_color");
        out->checked = js_bool(v, "checked", false);
        return true;
    }
    if (json_is_array(v)) {
        out->id = (int)json_integer_value(json_array_get(v, 0));
        json_t *cap = json_array_get(v, 1);
        if (json_is_string(cap))
            out->caption = str_sanitize_copy(json_string_value(cap));
        json_t *fg = json_array_get(v, 2);
        if (json_is_string(fg))
            out->fg_color = str_sanitize_copy(json_string_value(fg));
        json_t *bg = json_array_get(v, 3);
        if (json_is_string(bg))
            out->bg_color = str_sanitize_copy(json_string_value(bg));
        return true;
    }
    return false;
}

static Label *parse_labels_array(json_t *arr, size_t *count)
{
    *count = 0;
    if (!arr || !json_is_array(arr))
        return NULL;
    size_t n = json_array_size(arr);
    if (n == 0)
        return NULL;
    Label *out = xcalloc(n, sizeof *out);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (parse_label_entry(json_array_get(arr, i), &out[j]))
            j++;
    }
    *count = j;
    return out;
}

static Attachment *parse_attachments(json_t *arr, size_t *count)
{
    *count = 0;
    if (!arr || !json_is_array(arr))
        return NULL;
    size_t n = json_array_size(arr);
    if (n == 0)
        return NULL;
    Attachment *out = xcalloc(n, sizeof *out);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        json_t *a = json_array_get(arr, i);
        if (!json_is_object(a))
            continue;
        out[j].url = js_str(a, "url");
        out[j].content_type = js_str(a, "content_type");
        j++;
    }
    *count = j;
    return out;
}

/* ----------------------------------------------------------------------- */
/* frees                                                                   */
/* ----------------------------------------------------------------------- */

void category_free(Category *arr, size_t n)
{
    if (!arr)
        return;
    for (size_t i = 0; i < n; i++)
        free(arr[i].title);
    free(arr);
}

void feed_free(Feed *arr, size_t n)
{
    if (!arr)
        return;
    for (size_t i = 0; i < n; i++) {
        free(arr[i].title);
        free(arr[i].feed_url);
        free(arr[i].last_error);
    }
    free(arr);
}

void label_free(Label *arr, size_t n)
{
    if (!arr)
        return;
    for (size_t i = 0; i < n; i++) {
        free(arr[i].caption);
        free(arr[i].fg_color);
        free(arr[i].bg_color);
    }
    free(arr);
}

static void headline_clear(Headline *h)
{
    free(h->guid);
    free(h->title);
    free(h->link);
    free(h->author);
    free(h->feed_title);
    free(h->site_url);
    free(h->note);
    free(h->excerpt);
    free(h->content);
    free(h->lang);
    free(h->comments_link);
    if (h->attachments) {
        for (size_t i = 0; i < h->nattachments; i++) {
            free(h->attachments[i].url);
            free(h->attachments[i].content_type);
        }
        free(h->attachments);
    }
    label_free(h->labels, h->nlabels);
}

void headline_free(Headline *arr, size_t n)
{
    if (!arr)
        return;
    for (size_t i = 0; i < n; i++)
        headline_clear(&arr[i]);
    free(arr);
}

/* ----------------------------------------------------------------------- */
/* parsers                                                                 */
/* ----------------------------------------------------------------------- */

Category *parse_categories(json_t *arr, size_t *count)
{
    *count = 0;
    if (!arr || !json_is_array(arr))
        return NULL;
    size_t n = json_array_size(arr);
    if (n == 0)
        return NULL;
    Category *out = xcalloc(n, sizeof *out);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        json_t *c = json_array_get(arr, i);
        if (!json_is_object(c))
            continue;
        out[j].id = js_int(c, "id", 0);
        out[j].title = js_str(c, "title");
        out[j].unread = js_int(c, "unread", 0);
        out[j].order_id = js_int(c, "order_id", 0);
        j++;
    }
    *count = j;
    return out;
}

Feed *parse_feeds(json_t *arr, size_t *count)
{
    *count = 0;
    if (!arr || !json_is_array(arr))
        return NULL;
    size_t n = json_array_size(arr);
    if (n == 0)
        return NULL;
    Feed *out = xcalloc(n, sizeof *out);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        json_t *f = json_array_get(arr, i);
        if (!json_is_object(f))
            continue;
        out[j].id = js_int(f, "id", 0);
        out[j].title = js_str(f, "title");
        out[j].feed_url = js_str(f, "feed_url");
        out[j].cat_id = js_int(f, "cat_id", 0);
        out[j].unread = js_int(f, "unread", 0);
        out[j].has_icon = js_bool(f, "has_icon", false);
        out[j].is_cat = js_bool(f, "is_cat", false);
        out[j].last_error = js_str(f, "last_error");
        out[j].last_updated = js_time(f, "last_updated");
        j++;
    }
    *count = j;
    return out;
}

Label *parse_labels(json_t *arr, size_t *count)
{
    return parse_labels_array(arr, count);
}

static void parse_headline_obj(json_t *h, Headline *out)
{
    memset(out, 0, sizeof *out);
    out->id = js_int(h, "id", 0);
    out->guid = js_str(h, "guid");
    out->title = js_str_decoded(h, "title");
    out->link = js_str(h, "link");
    out->author = js_str_decoded(h, "author");
    out->feed_title = js_str_decoded(h, "feed_title");
    out->site_url = js_str(h, "site_url");
    out->feed_id = js_int(h, "feed_id", 0);
    out->unread = js_bool(h, "unread", false);
    out->marked = js_bool(h, "marked", false);
    out->published = js_bool(h, "published", false);
    out->is_updated = js_bool(h, "is_updated", false);
    out->score = js_int(h, "score", 0);
    out->updated = js_time(h, "updated");
    out->note = js_str(h, "note");
    out->excerpt = js_str(h, "excerpt");
    out->content = js_str(h, "content");
    out->lang = js_str(h, "lang");
    out->comments_count = js_int(h, "comments_count", 0);
    out->comments_link = js_str(h, "comments_link");
    out->attachments = parse_attachments(json_object_get(h, "attachments"),
                                         &out->nattachments);
    out->labels = parse_labels_array(json_object_get(h, "labels"),
                                     &out->nlabels);
}

Headline *parse_headlines(json_t *arr, size_t *count)
{
    *count = 0;
    if (!arr || !json_is_array(arr))
        return NULL;
    size_t n = json_array_size(arr);
    if (n == 0)
        return NULL;
    Headline *out = xcalloc(n, sizeof *out);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        json_t *h = json_array_get(arr, i);
        if (!json_is_object(h))
            continue;
        parse_headline_obj(h, &out[j]);
        j++;
    }
    *count = j;
    return out;
}

Headline *parse_article(json_t *arr, size_t *count)
{
    return parse_headlines(arr, count);
}

Counter parse_counters(json_t *obj)
{
    Counter c;
    memset(&c, 0, sizeof c);
    if (!obj)
        return c;
    /* Counters::get_all() nests some values; support both flat and nested. */
    c.total = js_int(obj, "total", 0);
    c.unread = js_int(obj, "unread", 0);
    c.marked = js_int(obj, "marked", 0);
    c.published = js_int(obj, "published", 0);
    c.feeds = js_int(obj, "feeds", 0);
    c.labels = js_int(obj, "labels", 0);
    c.fresh = js_int(obj, "fresh", 0);

    json_t *sub;
    if ((sub = json_object_get(obj, "global"))) {
        c.unread = js_int(sub, "unread", c.unread);
        c.marked = js_int(sub, "marked", c.marked);
        c.published = js_int(sub, "published", c.published);
    }
    if ((sub = json_object_get(obj, "subscribed"))) {
        if (c.unread == 0)
            c.unread = js_int(sub, "unread", c.unread);
    }
    return c;
}

/* ----------------------------------------------------------------------- */
/* classification                                                          */
/* ----------------------------------------------------------------------- */

bool model_is_virtual_feed(int id)
{
    switch (id) {
    case FEED_ARCHIVED:
    case FEED_STARRED:
    case FEED_PUBLISHED:
    case FEED_FRESH:
    case FEED_ALL:
    case FEED_RECENTLY_READ:
        return true;
    default:
        return false;
    }
}

const char *model_virtual_feed_title(int id)
{
    switch (id) {
    case FEED_ARCHIVED:
        return "Archived";
    case FEED_STARRED:
        return "Starred";
    case FEED_PUBLISHED:
        return "Published";
    case FEED_FRESH:
        return "Fresh";
    case FEED_ALL:
        return "All articles";
    case FEED_RECENTLY_READ:
        return "Recently read";
    default:
        return "Feed";
    }
}

ItemKind model_classify_feed_id(int id)
{
    if (id > 0)
        return ITEM_FEED;
    if (model_is_virtual_feed(id))
        return ITEM_VIRTUAL;
    if (id < LABEL_BASE_INDEX)
        return ITEM_LABEL;
    if (id <= PLUGIN_FEED_BASE_INDEX)
        return ITEM_PLUGIN;
    return ITEM_VIRTUAL;
}
