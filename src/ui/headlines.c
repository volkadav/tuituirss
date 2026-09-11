// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

void headlines_free(App *app) {
  headline_free(app->heads, app->nheads);
  app->heads = NULL;
  app->nheads = 0;
  app->head_total_loaded = 0;
  app->head_has_more = false;
}

int headlines_load(App *app, int feed_id, bool is_cat, const char *title) {
  headlines_free(app);
  app->cur_id = feed_id;
  app->cur_is_cat = is_cat;
  /* Duplicate before freeing: callers may pass app->cur_title itself. */
  char *new_title = xstrdup(title ? title : "");
  free(app->cur_title);
  app->cur_title = new_title;
  app->head_limit = app->head_rows > 0 ? app->head_rows : 20;
  app->head_sel = 0;
  app->head_top = 0;

  Headline *h = NULL;
  size_t n = 0;
  if (api_get_headlines(app->api, feed_id, is_cat, app->head_limit, 0,
                        app->view_mode, app->filter, false, true, &h,
                        &n) != 0) {
    ui_status(app, true, "Failed to load headlines: %s",
              api_last_error_string(app->api));
    return -1;
  }
  app->heads = h;
  app->nheads = n;
  app->head_total_loaded = (int)n;
  app->head_has_more = (n >= (size_t)app->head_limit);

  /* Pre-select the first unread headline if any. */
  for (size_t i = 0; i < n; i++) {
    if (app->heads[i].unread) {
      app->head_sel = (int)i;
      break;
    }
  }
  ui_status(app, false, "%zu headline(s)", n);
  return 0;
}

int headlines_load_more(App *app) {
  if (!app->head_has_more)
    return 0;
  int skip = (int)app->nheads;
  Headline *more = NULL;
  size_t nmore = 0;
  if (api_get_headlines(app->api, app->cur_id, app->cur_is_cat, app->head_limit,
                        skip, app->view_mode, app->filter, false, true, &more,
                        &nmore) != 0) {
    ui_status(app, true, "Failed to load more: %s",
              api_last_error_string(app->api));
    return -1;
  }
  if (nmore == 0) {
    app->head_has_more = false;
    headline_free(more, nmore);
    return 0;
  }
  size_t total = app->nheads + nmore;
  Headline *merged = xcalloc(total, sizeof(Headline));
  memcpy(merged, app->heads, app->nheads * sizeof(Headline));
  memcpy(merged + app->nheads, more, nmore * sizeof(Headline));
  free(app->heads); /* container only; contents moved */
  free(more);
  app->heads = merged;
  app->nheads = total;
  app->head_total_loaded = (int)total;
  app->head_has_more = (nmore >= (size_t)app->head_limit);
  ui_status(app, false, "Loaded %zu headline(s)", total);
  return 0;
}

Headline *headline_current(App *app) {
  if (app->head_sel < 0 || (size_t)app->head_sel >= app->nheads)
    return NULL;
  return &app->heads[app->head_sel];
}

void headlines_move(App *app, int delta) {
  if (app->nheads == 0)
    return;
  int idx = app->head_sel + delta;
  if (idx < 0)
    idx = 0;
  if ((size_t)idx >= app->nheads) {
    if (app->head_has_more) {
      if (headlines_load_more(app) != 0)
        return;
    }
    if ((size_t)idx >= app->nheads)
      idx = (int)app->nheads - 1;
  }
  app->head_sel = idx;
  if (app->head_sel < app->head_top)
    app->head_top = app->head_sel;
  if (app->head_rows > 0 && app->head_sel >= app->head_top + app->head_rows)
    app->head_top = app->head_sel - app->head_rows + 1;
}

void headlines_home(App *app) {
  app->head_sel = 0;
  app->head_top = 0;
}

void headlines_end(App *app) {
  while (app->head_has_more)
    if (headlines_load_more(app) != 0)
      break;
  if (app->nheads > 0)
    app->head_sel = (int)app->nheads - 1;
  if (app->head_rows > 0 && app->head_sel >= app->head_top + app->head_rows)
    app->head_top = app->head_sel - app->head_rows + 1;
}

void headlines_draw(App *app) {
  WINDOW *w = app->head_win;
  if (!w)
    return;
  werase(w);
  box(w, 0, 0);
  int width = app->right_w - 2;

  for (int r = 0; r < app->head_rows; r++) {
    int idx = app->head_top + r;
    if (idx < 0 || (size_t)idx >= app->nheads)
      break;
    Headline *h = &app->heads[idx];
    int y = r + 1;
    bool selected = (idx == app->head_sel);

    int attr;
    if (selected)
      attr = COLOR_PAIR(CP_SELECTED) | A_BOLD;
    else if (h->unread)
      attr = COLOR_PAIR(CP_UNREAD) | A_BOLD;
    else
      attr = COLOR_PAIR(CP_READ);

    if (selected)
      render_fill(w, y, 1, width, attr);

    char marks[8];
    snprintf(marks, sizeof marks, "%c%c%c", h->unread ? 'o' : ' ',
             h->marked ? '*' : ' ', h->published ? 'p' : ' ');
    render_text(w, y, 1, 4, marks, attr);

    char age[32];
    format_age(h->updated, age, sizeof age);
    int agelen = (int)utf8_width(age);
    render_text_justify(w, y, 1, width, age, attr, true);
    int titlew = width - 4 - agelen - 1;
    render_text(w, y, 5, titlew, h->title ? h->title : "", attr);
  }
  wnoutrefresh(w);
}

int headlines_activate(App *app) {
  Headline *h = headline_current(app);
  if (!h)
    return -1;
  if (article_load(app, h->id) != 0)
    return -1;
  app->focus = PANE_ARTICLE;
  return 0;
}
