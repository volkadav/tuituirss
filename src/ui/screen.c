// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void on_fatal_signal(int sig) {
  endwin();
  _exit(128 + sig);
}

static const char *g_view_modes[] = {
    VIEW_ALL_ARTICLES, VIEW_UNREAD,    VIEW_ADAPTIVE, VIEW_MARKED,
    VIEW_UPDATED,      VIEW_PUBLISHED, VIEW_HAS_NOTE,
};
static const size_t g_view_mode_count =
    sizeof g_view_modes / sizeof g_view_modes[0];

App *app_new(Config *cfg, ApiClient *api) {
  App *app = xcalloc(1, sizeof *app);
  app->cfg = cfg;
  app->api = api;
  app->focus = PANE_FEEDS;
  app->view_mode = VIEW_ALL_ARTICLES;
  app->status_error = false;
  snprintf(app->status, sizeof app->status, "Ready");
  return app;
}

void app_free(App *app) {
  if (!app)
    return;
  feedlist_free(app);
  headlines_free(app);
  article_clear(app);
  free(app->cur_title);
  free(app->filter);
  if (app->feed_win)
    delwin(app->feed_win);
  if (app->head_win)
    delwin(app->head_win);
  if (app->art_win)
    delwin(app->art_win);
  if (app->status_win)
    delwin(app->status_win);
  free(app);
}

void ui_status(App *app, bool error, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(app->status, sizeof app->status, fmt, ap);
  va_end(ap);
  app->status_error = error;
  app->busy = false;
}

void ui_layout(App *app) {
  getmaxyx(stdscr, app->rows, app->cols);

  if (app->feed_win) {
    delwin(app->feed_win);
    app->feed_win = NULL;
  }
  if (app->head_win) {
    delwin(app->head_win);
    app->head_win = NULL;
  }
  if (app->art_win) {
    delwin(app->art_win);
    app->art_win = NULL;
  }
  if (app->status_win) {
    delwin(app->status_win);
    app->status_win = NULL;
  }

  app->main_h = app->rows - 1;
  if (app->main_h < 3)
    app->main_h = 3;

  int feed_w = app->cols / 4;
  if (feed_w < 20)
    feed_w = app->cols / 3;
  if (feed_w < 14)
    feed_w = app->cols / 2;
  if (feed_w > 44)
    feed_w = 44;
  if (feed_w < 8)
    feed_w = 8;
  if (feed_w > app->cols - 10)
    feed_w = app->cols - 10;
  app->feed_w = feed_w;
  app->right_w = app->cols - feed_w;
  if (app->right_w < 4)
    app->right_w = 4;

  int head_h = 0;
  int art_h;
  if (app->article_only) {
    art_h = app->main_h;
  } else {
    head_h = app->main_h / 2;
    if (head_h < 4)
      head_h = 4;
    if (head_h > app->main_h - 4)
      head_h = app->main_h - 4;
    if (head_h < 3)
      head_h = app->main_h / 2;
    art_h = app->main_h - head_h;
  }

  app->feed_win = newwin(app->main_h, app->feed_w, 0, 0);
  if (!app->article_only)
    app->head_win = newwin(head_h, app->right_w, 0, app->feed_w);
  app->art_win = newwin(art_h, app->right_w, head_h, app->feed_w);
  app->status_win = newwin(1, app->cols, app->rows - 1, 0);

  keypad(app->feed_win, TRUE);
  if (app->head_win)
    keypad(app->head_win, TRUE);
  keypad(app->art_win, TRUE);
  keypad(app->status_win, TRUE);

  app->side_rows = app->main_h - 2;
  app->head_rows = app->head_win ? head_h - 2 : 0;
  if (app->head_rows > 0)
    app->head_limit = app->head_rows; /* re-clamp page size on resize */
  app->art_rows = art_h - 2;
  app->art_cols = app->right_w;

  if (app->focus == PANE_HEADLINES && app->article_only)
    app->focus = PANE_ARTICLE;

  if (app->article && app->art_wrap_width != app->art_cols - 2)
    article_rebuild(app);

  clearok(stdscr, TRUE);
}

static void draw_all(App *app) {
  if (app->need_resize) {
    ui_layout(app);
    app->need_resize = false;
  }
  feedlist_draw(app);
  headlines_draw(app);
  article_draw(app);
  status_draw(app);
  doupdate();
}

void app_refresh_counters(App *app) {
  Counter c;
  if (api_get_counters(app->api, &c) == 0)
    app->counters = c;
}

static void bump_sidebar_unread(App *app, int feed_id, int delta) {
  if (delta == 0)
    return;
  for (size_t i = 0; i < app->nitems; i++) {
    if (app->items[i].kind == SI_ITEM && app->items[i].id == feed_id) {
      app->items[i].unread += delta;
      if (app->items[i].unread < 0)
        app->items[i].unread = 0;
      break;
    }
  }
}

static void apply_field(Headline *h, int field, int mode, const char *data) {
  if (!h)
    return;
  switch (field) {
  case UA_FIELD_MARKED:
    h->marked = (mode == UA_MODE_TOGGLE) ? !h->marked : (mode == UA_MODE_TRUE);
    break;
  case UA_FIELD_PUBLISHED:
    h->published =
        (mode == UA_MODE_TOGGLE) ? !h->published : (mode == UA_MODE_TRUE);
    break;
  case UA_FIELD_UNREAD:
    h->unread = (mode == UA_MODE_TOGGLE) ? !h->unread : (mode == UA_MODE_TRUE);
    break;
  case UA_FIELD_SCORE:
    if (data)
      h->score = atoi(data);
    break;
  case UA_FIELD_NOTE:
    free(h->note);
    h->note = data ? xstrdup(data) : NULL;
    break;
  }
}

static Headline *mutation_target(App *app) {
  if (app->focus == PANE_ARTICLE && app->article)
    return app->article;
  return headline_current(app);
}

static void mutate(App *app, int field, int mode, const char *data) {
  Headline *h = mutation_target(app);
  if (!h) {
    ui_status(app, true, "No headline selected");
    return;
  }
  int id = h->id;
  int was_unread = h->unread;
  if (api_update_article(app->api, &id, 1, mode, field, data) != 0) {
    ui_status(app, true, "Update failed: %s", api_last_error_string(app->api));
    return;
  }
  apply_field(h, field, mode, data);
  /* mirror onto the same headline in the list, if present */
  for (size_t i = 0; i < app->nheads; i++) {
    if (app->heads[i].id == id) {
      apply_field(&app->heads[i], field, mode, data);
      break;
    }
  }
  if (app->article && app->article->id == id && app->article != h)
    apply_field(app->article, field, mode, data);

  if (field == UA_FIELD_UNREAD && h->unread != was_unread)
    bump_sidebar_unread(app, app->cur_id, h->unread ? 1 : -1);

  if (field == UA_FIELD_NOTE)
    article_rebuild(app);

  app_refresh_counters(app);
  ui_status(app, false, "Updated");
}

static void next_unread(App *app) {
  if (app->nheads == 0)
    return;
  size_t start = (size_t)app->head_sel + 1;
  for (size_t k = 0; k < app->nheads; k++) {
    size_t i = (start + k) % app->nheads;
    if (app->heads[i].unread) {
      app->head_sel = (int)i;
      if (app->head_sel < app->head_top)
        app->head_top = app->head_sel;
      if (app->head_rows > 0 && app->head_sel >= app->head_top + app->head_rows)
        app->head_top = app->head_sel - app->head_rows + 1;
      return;
    }
  }
  ui_status(app, false, "No unread headlines");
}

static void cycle_view(App *app) {
  size_t cur = 0;
  for (size_t i = 0; i < g_view_mode_count; i++)
    if (strcmp(g_view_modes[i], app->view_mode) == 0)
      cur = i;
  app->view_mode = g_view_modes[(cur + 1) % g_view_mode_count];
  if (app->cur_title)
    headlines_load(app, app->cur_id, app->cur_is_cat, app->cur_title);
  ui_status(app, false, "View: %s", app->view_mode);
}

static void do_filter(App *app) {
  char *f = dialog_input(app, "Filter headlines (empty to clear)",
                         app->filter ? app->filter : "");
  if (!f)
    return;
  free(app->filter);
  if (*f) {
    app->filter = f;
  } else {
    free(f);
    app->filter = NULL;
  }
  if (app->cur_title)
    headlines_load(app, app->cur_id, app->cur_is_cat, app->cur_title);
  ui_status(app, false, "Filter: %s", app->filter ? app->filter : "(none)");
}

static void do_subscribe(App *app) {
  char *url = dialog_input(app, "Subscribe to feed URL", "");
  if (!url || !*url) {
    free(url);
    return;
  }
  if (api_subscribe_feed(app->api, url, 0, NULL, NULL) != 0) {
    ui_status(app, true, "Subscribe failed: %s",
              api_last_error_string(app->api));
  } else {
    ui_status(app, false, "Subscribed to %s", url);
    feedlist_load(app);
  }
  free(url);
}

static void do_unsubscribe(App *app) {
  SidebarItem *it = feedlist_current(app);
  if (!it || it->source != SRC_FEED) {
    ui_status(app, true, "Select a feed to unsubscribe");
    return;
  }
  char q[512];
  snprintf(q, sizeof q, "Unsubscribe from \"%s\"?", it->title);
  if (!dialog_confirm(app, "Unsubscribe", q))
    return;
  int id = it->id;
  if (api_unsubscribe_feed(app->api, id) != 0) {
    ui_status(app, true, "Unsubscribe failed: %s",
              api_last_error_string(app->api));
  } else {
    ui_status(app, false, "Unsubscribed");
    feedlist_load(app);
  }
}

static void do_update_feed(App *app) {
  SidebarItem *it = feedlist_current(app);
  if (!it || it->source != SRC_FEED) {
    ui_status(app, true, "Select a feed to update");
    return;
  }
  int id = it->id;
  ui_status(app, false, "Updating feed...");
  draw_all(app);
  if (api_update_feed(app->api, id) != 0)
    ui_status(app, true, "Update failed: %s", api_last_error_string(app->api));
  else
    ui_status(app, false, "Feed update requested");
}

static void do_catchup(App *app) {
  int id;
  bool is_cat;
  if (app->focus == PANE_FEEDS) {
    SidebarItem *it = feedlist_current(app);
    if (!it)
      return;
    id = it->id;
    is_cat = it->is_cat;
  } else {
    id = app->cur_id;
    is_cat = app->cur_is_cat;
  }
  if (api_catchup_feed(app->api, id, is_cat, "all") != 0) {
    ui_status(app, true, "Catchup failed: %s", api_last_error_string(app->api));
    return;
  }
  for (size_t i = 0; i < app->nheads; i++)
    app->heads[i].unread = false;
  if (app->article)
    app->article->unread = false;
  feedlist_load(app);
  ui_status(app, false, "Marked as read");
}

static void do_note(App *app) {
  Headline *h = mutation_target(app);
  if (!h)
    return;
  char *note =
      dialog_input(app, "Note (empty to clear)", h->note ? h->note : "");
  if (!note)
    return;
  mutate(app, UA_FIELD_NOTE, UA_MODE_TRUE, note);
  free(note);
}

static void do_score(App *app, int delta) {
  Headline *h = mutation_target(app);
  if (!h)
    return;
  char data[32];
  snprintf(data, sizeof data, "%d", h->score + delta);
  mutate(app, UA_FIELD_SCORE, UA_MODE_TRUE, data);
}

static void do_labels(App *app) {
  Headline *h = mutation_target(app);
  if (!h)
    return;
  int id = h->id;
  dialog_label_picker(app, &id, 1);
}

static void focus_next(App *app) {
  if (app->article_only) {
    app->focus = (app->focus == PANE_FEEDS) ? PANE_ARTICLE : PANE_FEEDS;
    return;
  }
  switch (app->focus) {
  case PANE_FEEDS:
    app->focus = PANE_HEADLINES;
    break;
  case PANE_HEADLINES:
    app->focus = PANE_ARTICLE;
    break;
  default:
    app->focus = PANE_FEEDS;
    break;
  }
}

static void focus_prev(App *app) {
  if (app->article_only) {
    app->focus = (app->focus == PANE_FEEDS) ? PANE_ARTICLE : PANE_FEEDS;
    return;
  }
  switch (app->focus) {
  case PANE_FEEDS:
    app->focus = PANE_ARTICLE;
    break;
  case PANE_HEADLINES:
    app->focus = PANE_FEEDS;
    break;
  default:
    app->focus = PANE_HEADLINES;
    break;
  }
}

static void go_back(App *app) {
  if (app->article_only && app->focus == PANE_ARTICLE) {
    app->article_only = false;
    app->need_resize = true;
    return;
  }
  if (app->focus == PANE_ARTICLE)
    app->focus = app->article_only ? PANE_FEEDS : PANE_HEADLINES;
  else if (app->focus == PANE_HEADLINES)
    app->focus = PANE_FEEDS;
}

static void handle_action(App *app, Action a) {
  switch (a) {
  case ACT_NONE:
    break;
  case ACT_QUIT:
    if (app->focus == PANE_FEEDS)
      app->running = false;
    else
      go_back(app);
    break;
  case ACT_BACK:
    go_back(app);
    break;
  case ACT_DOWN:
    if (app->focus == PANE_ARTICLE)
      article_scroll(app, 1);
    else if (app->focus == PANE_FEEDS)
      feedlist_move(app, 1);
    else
      headlines_move(app, 1);
    break;
  case ACT_UP:
    if (app->focus == PANE_ARTICLE)
      article_scroll(app, -1);
    else if (app->focus == PANE_FEEDS)
      feedlist_move(app, -1);
    else
      headlines_move(app, -1);
    break;
  case ACT_FIRST:
    if (app->focus == PANE_ARTICLE)
      app->art_scroll = 0;
    else if (app->focus == PANE_FEEDS)
      feedlist_home(app);
    else
      headlines_home(app);
    break;
  case ACT_LAST:
    if (app->focus == PANE_ARTICLE) {
      app->art_scroll = (int)app->art_nlines;
      article_scroll(app, 0);
    } else if (app->focus == PANE_FEEDS)
      feedlist_end(app);
    else
      headlines_end(app);
    break;
  case ACT_FOCUS_NEXT:
    focus_next(app);
    break;
  case ACT_FOCUS_PREV:
    focus_prev(app);
    break;
  case ACT_OPEN:
    if (app->focus == PANE_FEEDS)
      feedlist_activate(app);
    else if (app->focus == PANE_HEADLINES)
      headlines_activate(app);
    break;
  case ACT_TOGGLE_ARTICLE: {
    Headline *h = headline_current(app);
    if (h)
      article_load(app, h->id);
    break;
  }
  case ACT_TOGGLE_READ:
    mutate(app, UA_FIELD_UNREAD, UA_MODE_TOGGLE, NULL);
    break;
  case ACT_TOGGLE_STAR:
    mutate(app, UA_FIELD_MARKED, UA_MODE_TOGGLE, NULL);
    break;
  case ACT_TOGGLE_PUBLISH:
    mutate(app, UA_FIELD_PUBLISHED, UA_MODE_TOGGLE, NULL);
    break;
  case ACT_NEXT_UNREAD:
    next_unread(app);
    break;
  case ACT_CATCHUP:
    do_catchup(app);
    break;
  case ACT_NOTE:
    do_note(app);
    break;
  case ACT_SCORE_UP:
    do_score(app, 1);
    break;
  case ACT_SCORE_DOWN:
    do_score(app, -1);
    break;
  case ACT_LABELS:
    do_labels(app);
    break;
  case ACT_CYCLE_VIEW:
    cycle_view(app);
    break;
  case ACT_FILTER:
    do_filter(app);
    break;
  case ACT_SUBSCRIBE:
    do_subscribe(app);
    break;
  case ACT_UNSUBSCRIBE:
    do_unsubscribe(app);
    break;
  case ACT_UPDATE_FEED:
    do_update_feed(app);
    break;
  case ACT_TOGGLE_LAYOUT:
    app->article_only = !app->article_only;
    app->need_resize = true;
    break;
  case ACT_REFRESH:
    feedlist_load(app);
    if (app->cur_title)
      headlines_load(app, app->cur_id, app->cur_is_cat, app->cur_title);
    ui_status(app, false, "Refreshed");
    break;
  case ACT_HELP:
    dialog_help(app);
    break;
  case ACT_LOAD_MORE:
    headlines_load_more(app);
    break;
  }
}

int ui_run(App *app) {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  set_escdelay(25);
  signal(SIGINT, on_fatal_signal);
  signal(SIGTERM, on_fatal_signal);
  render_init();
  ui_layout(app);

  ui_busy(app, "Loading feeds...");
  if (feedlist_load(app) != 0) {
    dialog_message(app, "Error", app->status);
  }
  app_refresh_counters(app);

  app->running = true;
  while (app->running) {
    draw_all(app);
    timeout(500);
    int ch = getch();
    if (ch == KEY_RESIZE) {
      app->need_resize = true;
      continue;
    }
    if (ch == ERR)
      continue;
    unsigned ctx = input_context(app);
    Action a = input_action(ch, ctx);
    handle_action(app, a);
  }

  endwin();
  return 0;
}

void ui_busy(App *app, const char *msg) {
  snprintf(app->status, sizeof app->status, "%s", msg);
  app->status_error = false;
  app->busy = true;
  /* Draw a best-effort frame so the message is visible. */
  feedlist_draw(app);
  headlines_draw(app);
  article_draw(app);
  status_draw(app);
  doupdate();
  app->busy = false;
}
