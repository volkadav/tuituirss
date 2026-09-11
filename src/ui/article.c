// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

void article_clear(App *app) {
  if (app->article) {
    headline_free(app->article, 1);
    app->article = NULL;
  }
  artline_free(app->art_lines, app->art_nlines);
  app->art_lines = NULL;
  app->art_nlines = 0;
  app->art_scroll = 0;
  app->art_wrap_width = 0;
}

static void add_blank(App *app, size_t *cap) {
  render_wrap("", app->art_wrap_width, COLOR_PAIR(CP_DEFAULT), &app->art_lines,
              &app->art_nlines, cap);
}

void article_rebuild(App *app) {
  artline_free(app->art_lines, app->art_nlines);
  app->art_lines = NULL;
  app->art_nlines = 0;
  app->art_scroll = 0;

  if (!app->article)
    return;

  int width = app->art_cols - 2;
  if (width < 4)
    width = 4;
  app->art_wrap_width = width;

  size_t cap = 0;
  Headline *a = app->article;

  render_wrap(a->title ? a->title : "(untitled)", width,
              COLOR_PAIR(CP_TITLE) | A_BOLD, &app->art_lines, &app->art_nlines,
              &cap);
  add_blank(app, &cap);

  char meta[1024];
  char when[64];
  format_datetime(a->updated, when, sizeof when);
  snprintf(meta, sizeof meta, "%s%s%s%s%s", a->feed_title ? a->feed_title : "",
           (a->author && *a->author) ? " · " : "",
           (a->author && *a->author) ? a->author : "", when[0] ? " · " : "",
           when);
  render_wrap(meta, width, COLOR_PAIR(CP_FEED), &app->art_lines,
              &app->art_nlines, &cap);

  if (a->link && *a->link) {
    render_wrap(a->link, width, COLOR_PAIR(CP_DEFAULT) | A_UNDERLINE,
                &app->art_lines, &app->art_nlines, &cap);
  }

  if (a->nlabels > 0) {
    char labels[1024] = "Labels: ";
    for (size_t i = 0; i < a->nlabels; i++) {
      if (i)
        strncat(labels, ", ", sizeof labels - strlen(labels) - 1);
      strncat(labels, a->labels[i].caption ? a->labels[i].caption : "",
              sizeof labels - strlen(labels) - 1);
    }
    render_wrap(labels, width, COLOR_PAIR(CP_NOTE), &app->art_lines,
                &app->art_nlines, &cap);
  }

  add_blank(app, &cap);

  autofree char *text = html_to_text(a->content ? a->content : "");
  if (!*text) {
    free(text);
    text = html_to_text(a->excerpt ? a->excerpt : "(no content)");
  }
  render_wrap(text, width, COLOR_PAIR(CP_DEFAULT), &app->art_lines,
              &app->art_nlines, &cap);

  if (a->note && *a->note) {
    add_blank(app, &cap);
    render_wrap("Note:", width, COLOR_PAIR(CP_NOTE) | A_BOLD, &app->art_lines,
                &app->art_nlines, &cap);
    render_wrap(a->note, width, COLOR_PAIR(CP_NOTE), &app->art_lines,
                &app->art_nlines, &cap);
  }

  if (a->nattachments > 0) {
    add_blank(app, &cap);
    render_wrap("Attachments:", width, COLOR_PAIR(CP_FEED) | A_BOLD,
                &app->art_lines, &app->art_nlines, &cap);
    for (size_t i = 0; i < a->nattachments; i++) {
      render_wrap(a->attachments[i].url ? a->attachments[i].url : "", width,
                  COLOR_PAIR(CP_DEFAULT), &app->art_lines, &app->art_nlines,
                  &cap);
    }
  }
}

int article_load(App *app, int article_id) {
  article_clear(app);

  Headline *arr = NULL;
  size_t n = 0;
  if (api_get_article(app->api, article_id, &arr, &n) != 0) {
    ui_status(app, true, "Failed to load article: %s",
              api_last_error_string(app->api));
    return -1;
  }
  if (n == 0) {
    headline_free(arr, n);
    ui_status(app, true, "Article not found");
    return -1;
  }
  app->article = xcalloc(1, sizeof(Headline));
  app->article[0] = arr[0];
  for (size_t i = 1; i < n; i++) {
    Headline tmp = arr[i];
    headline_free(&tmp, 1);
  }
  free(arr);
  article_rebuild(app);
  return 0;
}

void article_scroll(App *app, int delta) {
  if (!app->article)
    return;
  int max = (int)app->art_nlines - app->art_rows;
  if (max < 0)
    max = 0;
  int s = app->art_scroll + delta;
  if (s < 0)
    s = 0;
  if (s > max)
    s = max;
  app->art_scroll = s;
}

void article_draw(App *app) {
  WINDOW *w = app->art_win;
  if (!w)
    return;
  werase(w);
  box(w, 0, 0);

  if (!app->article) {
    render_text(w, 1, 2, app->art_cols - 3, "(select a headline to read)",
                COLOR_PAIR(CP_READ));
    wnoutrefresh(w);
    return;
  }

  for (int r = 0; r < app->art_rows; r++) {
    int idx = app->art_scroll + r;
    if (idx < 0 || (size_t)idx >= app->art_nlines)
      break;
    ArtLine *l = &app->art_lines[idx];
    render_text(w, r + 1, 1, app->art_cols - 2, l->text, l->attr);
  }
  wnoutrefresh(w);
}
