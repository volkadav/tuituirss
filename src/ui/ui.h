// SPDX-License-Identifier: MIT
#ifndef TUIIRSS_UI_H
#define TUIIRSS_UI_H

#include <stdbool.h>
#include <stddef.h>

#include <ncurses.h>

#include "api/api.h"
#include "config/config.h"
#include "model/model.h"

/* ---- colour pairs ----------------------------------------------------- */
enum {
  CP_DEFAULT = 1,
  CP_SELECTED,
  CP_UNREAD,
  CP_READ,
  CP_TITLE,
  CP_STATUS,
  CP_ERROR,
  CP_FEED,
  CP_SECTION,
  CP_STARRED,
  CP_NOTE,
};

/* ---- panes ------------------------------------------------------------ */
typedef enum { PANE_FEEDS = 0, PANE_HEADLINES, PANE_ARTICLE } Pane;

/* ---- actions ---------------------------------------------------------- */
typedef enum {
  ACT_NONE = 0,
  ACT_QUIT,
  ACT_BACK,
  ACT_DOWN,
  ACT_UP,
  ACT_FIRST,
  ACT_LAST,
  ACT_FOCUS_NEXT,
  ACT_FOCUS_PREV,
  ACT_OPEN,
  ACT_TOGGLE_ARTICLE,
  ACT_TOGGLE_READ,
  ACT_TOGGLE_STAR,
  ACT_TOGGLE_PUBLISH,
  ACT_NEXT_UNREAD,
  ACT_CATCHUP,
  ACT_NOTE,
  ACT_SCORE_UP,
  ACT_SCORE_DOWN,
  ACT_LABELS,
  ACT_CYCLE_VIEW,
  ACT_FILTER,
  ACT_SUBSCRIBE,
  ACT_UNSUBSCRIBE,
  ACT_UPDATE_FEED,
  ACT_TOGGLE_LAYOUT,
  ACT_REFRESH,
  ACT_HELP,
  ACT_LOAD_MORE,
} Action;

#define CTX_GLOBAL 0x01u
#define CTX_SIDEBAR 0x02u
#define CTX_HEADLINES 0x04u
#define CTX_ARTICLE 0x08u
#define CTX_LIST (CTX_SIDEBAR | CTX_HEADLINES)

/* ---- sidebar ---------------------------------------------------------- */
typedef enum {
  SI_SECTION, /* non-selectable header */
  SI_ITEM,
} SidebarKind;

typedef enum {
  SRC_VIRTUAL,
  SRC_LABEL,
  SRC_CATEGORY,
  SRC_FEED,
} SidebarSource;

typedef struct {
  SidebarKind kind;
  SidebarSource source;
  int id; /* feed / category / label id */
  char *title;
  int unread;
  int depth;
  bool is_cat;
  bool expanded;
} SidebarItem;

/* ---- wrapped article line -------------------------------------------- */
typedef struct {
  char *text;
  int attr;
} ArtLine;

/* ---- application state ------------------------------------------------ */
typedef struct {
  Config *cfg;
  ApiClient *api;

  int rows, cols;
  WINDOW *feed_win;
  WINDOW *head_win;
  WINDOW *art_win;
  WINDOW *status_win;
  int feed_w, right_w, main_h;
  Pane focus;
  bool article_only;
  bool running;
  bool need_resize;

  /* sidebar */
  SidebarItem *items;
  size_t nitems;
  size_t items_cap;
  int side_sel;
  int side_top;
  int side_rows;

  /* headlines */
  Headline *heads;
  size_t nheads;
  int head_sel;
  int head_top;
  int head_rows;
  int head_limit;
  bool head_has_more;
  int head_total_loaded;

  /* current feed selection */
  int cur_id;
  bool cur_is_cat;
  char *cur_title;
  const char *view_mode;
  char *filter;

  /* article */
  Headline *article;
  ArtLine *art_lines;
  size_t art_nlines;
  int art_scroll;
  int art_rows;
  int art_cols;
  int art_wrap_width;

  /* status */
  char status[512];
  bool status_error;
  Counter counters;

  /* async-ish spinner */
  bool busy;
} App;

/* ---- screen.c --------------------------------------------------------- */
App *app_new(Config *cfg, ApiClient *api);
void app_free(App *app);
int ui_run(App *app);
void ui_layout(App *app);
void ui_status(App *app, bool error, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void ui_busy(App *app, const char *msg);
void app_refresh_counters(App *app);

/* ---- feedlist.c ------------------------------------------------------- */
int feedlist_load(App *app);
void feedlist_draw(App *app);
void feedlist_move(App *app, int delta);
void feedlist_home(App *app);
void feedlist_end(App *app);
SidebarItem *feedlist_current(App *app);
int feedlist_activate(App *app);
void feedlist_free(App *app);

/* ---- headlines.c ------------------------------------------------------ */
int headlines_load(App *app, int feed_id, bool is_cat, const char *title);
int headlines_load_more(App *app);
void headlines_draw(App *app);
void headlines_move(App *app, int delta);
void headlines_home(App *app);
void headlines_end(App *app);
Headline *headline_current(App *app);
int headlines_activate(App *app);
void headlines_free(App *app);

/* ---- article.c -------------------------------------------------------- */
int article_load(App *app, int article_id);
void article_clear(App *app);
void article_draw(App *app);
void article_scroll(App *app, int delta);
void article_rebuild(App *app);

/* ---- status.c --------------------------------------------------------- */
void status_draw(App *app);

/* ---- dialog.c --------------------------------------------------------- */
char *dialog_input(App *app, const char *title, const char *initial);
int dialog_confirm(App *app, const char *title, const char *question);
void dialog_message(App *app, const char *title, const char *msg);
int dialog_label_picker(App *app, const int *ids, size_t n);
void dialog_help(App *app);

/* ---- render.c --------------------------------------------------------- */
void render_init(void);
void render_text(WINDOW *w, int y, int x, int maxw, const char *s, int attr);
void render_text_justify(WINDOW *w, int y, int x, int maxw, const char *s,
                         int attr, bool right);
void render_fill(WINDOW *w, int y, int x, int n, int attr);
/* Append wrapped lines of `text` to *lines / *n at `width`, each with attr. */
void render_wrap(const char *text, int width, int attr, ArtLine **lines,
                 size_t *n, size_t *cap);
void artline_free(ArtLine *lines, size_t n);

/* ---- input.c ---------------------------------------------------------- */
unsigned input_context(App *app);
Action input_action(int ch, unsigned ctx);
char *input_help_text(void);

#endif /* TUIIRSS_UI_H */
