// SPDX-License-Identifier: MIT
#ifndef TUIIRSS_MODEL_H
#define TUIIRSS_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <jansson.h>

/* ---- special IDs (mirrors classes/Feeds.php + include/functions.php) --- */
#define FEED_ARCHIVED 0
#define FEED_STARRED (-1)
#define FEED_PUBLISHED (-2)
#define FEED_FRESH (-3)
#define FEED_ALL (-4)
#define FEED_RECENTLY_READ (-6)
#define FEED_ERROR (-7)

#define CATEGORY_UNCATEGORIZED 0
#define CATEGORY_SPECIAL (-1)
#define CATEGORY_LABELS (-2)
#define CATEGORY_ALL_EXCEPT_VIRTUAL (-3)
#define CATEGORY_ALL (-4)

#define LABEL_BASE_INDEX (-1024)
#define PLUGIN_FEED_BASE_INDEX (-128)

/* ---- view modes ------------------------------------------------------- */
#define VIEW_ALL_ARTICLES "all_articles"
#define VIEW_UNREAD "unread"
#define VIEW_ADAPTIVE "adaptive"
#define VIEW_MARKED "marked"
#define VIEW_UPDATED "updated"
#define VIEW_PUBLISHED "published"
#define VIEW_HAS_NOTE "has_note"

/* ---- updateArticle fields / modes ------------------------------------ */
#define UA_FIELD_MARKED 0
#define UA_FIELD_PUBLISHED 1
#define UA_FIELD_UNREAD 2
#define UA_FIELD_NOTE 3
#define UA_FIELD_SCORE 4

#define UA_MODE_FALSE 0
#define UA_MODE_TRUE 1
#define UA_MODE_TOGGLE 2

/* ---- data types ------------------------------------------------------- */

typedef struct {
  int id;
  char *title;
  int unread;
  int order_id;
} Category;

typedef struct {
  int id;
  char *title;
  char *feed_url;
  int cat_id;
  int unread;
  bool has_icon;
  bool is_cat;
  char *last_error;
  time_t last_updated;
} Feed;

typedef struct {
  int id; /* label feed-id (negative, see LABEL_BASE_INDEX) */
  char *caption;
  char *fg_color;
  char *bg_color;
  bool checked;
} Label;

typedef struct {
  char *url;
  char *content_type;
} Attachment;

typedef struct {
  int id;
  char *guid;
  char *title;
  char *link;
  char *author;
  char *feed_title;
  char *site_url;
  int feed_id;
  bool unread;
  bool marked;
  bool published;
  bool is_updated;
  int score;
  time_t updated;
  char *note;
  char *excerpt;
  char *content;
  char *lang;
  int comments_count;
  char *comments_link;
  Attachment *attachments;
  size_t nattachments;
  Label *labels;
  size_t nlabels;
} Headline;

typedef struct {
  int total;
  int unread;
  int marked;
  int published;
  int feeds;
  int labels;
  int fresh;
} Counter;

/* ---- ownership / lifecycle ------------------------------------------- */

void category_free(Category *arr, size_t n);
void feed_free(Feed *arr, size_t n);
void label_free(Label *arr, size_t n);
void headline_free(Headline *arr, size_t n);

/* ---- parsers (content JSON -> owned structs) ------------------------- */

/* Each returns a newly allocated array (or NULL with *count == 0). */
Category *parse_categories(json_t *arr, size_t *count);
Feed *parse_feeds(json_t *arr, size_t *count);
Label *parse_labels(json_t *arr, size_t *count);
Headline *parse_headlines(json_t *arr, size_t *count);
Headline *parse_article(json_t *arr, size_t *count);
Counter parse_counters(json_t *obj);

/* ---- classification helpers ------------------------------------------ */

typedef enum {
  ITEM_FEED = 0, /* regular feed (id > 0) */
  ITEM_CATEGORY, /* category (id >= 0 but used as is_cat) */
  ITEM_LABEL,    /* label feed-id (LABEL_BASE_INDEX < id < 0) */
  ITEM_VIRTUAL,  /* special virtual feed (id <= 0, known) */
  ITEM_PLUGIN    /* plugin feed (PLUGIN_FEED_BASE_INDEX .. ) */
} ItemKind;

ItemKind model_classify_feed_id(int id);
bool model_is_virtual_feed(int id);
const char *model_virtual_feed_title(int id);

#endif /* TUIIRSS_MODEL_H */
