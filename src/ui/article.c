// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

static void article_links_free(App *app) {
  if (app->art_links) {
    for (size_t i = 0; i < app->art_nlinks; i++) {
      free(app->art_links[i].url);
    }
    free(app->art_links);
  }
  app->art_links = NULL;
  app->art_nlinks = 0;
  app->art_link_sel = -1;
}

void article_clear(App *app) {
  if (app->article) {
    headline_free(app->article, 1);
    app->article = NULL;
  }
  artline_free(app->art_lines, app->art_nlines);
  app->art_lines = NULL;
  app->art_nlines = 0;
  article_links_free(app);
  app->art_scroll = 0;
  app->art_wrap_width = 0;
}

static void add_blank(App *app, size_t *cap) {
  render_wrap("", app->art_wrap_width, COLOR_PAIR(CP_DEFAULT), &app->art_lines,
              &app->art_nlines, cap);
}

/* Append a URL to a de-duplicated, dynamically grown string array. */
static void url_add(char ***urls, size_t *n, size_t *cap, const char *url) {
  if (!url || !*url) {
    return;
  }
  for (size_t i = 0; i < *n; i++) {
    if (strcmp((*urls)[i], url) == 0) {
      return;
    }
  }
  if (*n == *cap) {
    *cap = *cap ? *cap * 2 : 8;
    *urls = xrealloc(*urls, *cap * sizeof **urls);
  }
  (*urls)[(*n)++] = xstrdup(url);
}

void article_rebuild(App *app) {
  artline_free(app->art_lines, app->art_nlines);
  app->art_lines = NULL;
  app->art_nlines = 0;
  article_links_free(app);
  app->art_scroll = 0;

  if (!app->article) {
    return;
  }

  int width = app->art_cols - 2;
  if (width < 4) {
    width = 4;
  }
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
      if (i) {
        strncat(labels, ", ", sizeof labels - strlen(labels) - 1);
      }
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

  /* ---- selectable link list ------------------------------------------ */
  char **urls = NULL;
  size_t nurls = 0, ucap = 0;
  url_add(&urls, &nurls, &ucap, a->link);
  url_add(&urls, &nurls, &ucap, a->comments_link);
  for (size_t i = 0; i < a->nattachments; i++) {
    url_add(&urls, &nurls, &ucap, a->attachments[i].url);
  }

  size_t ncontent = 0;
  char **content_urls = url_extract(text, &ncontent);
  for (size_t i = 0; i < ncontent; i++) {
    url_add(&urls, &nurls, &ucap, content_urls[i]);
  }
  url_free(content_urls, ncontent);

  if (nurls > 0) {
    add_blank(app, &cap);
    render_wrap("Links:", width, COLOR_PAIR(CP_FEED) | A_BOLD, &app->art_lines,
                &app->art_nlines, &cap);
    app->art_links = xcalloc(nurls, sizeof(ArtLink));
    for (size_t i = 0; i < nurls; i++) {
      size_t before = app->art_nlines;
      render_wrap(urls[i], width, COLOR_PAIR(CP_DEFAULT), &app->art_lines,
                  &app->art_nlines, &cap);
      app->art_links[i].url = urls[i]; /* take ownership */
      app->art_links[i].line = before;
      app->art_links[i].nlines = app->art_nlines - before;
    }
    app->art_nlinks = nurls;
    free(urls);
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
  if (!app->article) {
    return;
  }
  int max = (int)app->art_nlines - app->art_rows;
  if (max < 0) {
    max = 0;
  }
  int s = app->art_scroll + delta;
  if (s < 0) {
    s = 0;
  }
  if (s > max) {
    s = max;
  }
  app->art_scroll = s;
}

/* Scroll so the selected link is visible. */
static void article_link_reveal(App *app) {
  if (app->art_link_sel < 0 || (size_t)app->art_link_sel >= app->art_nlinks) {
    return;
  }
  ArtLink *l = &app->art_links[app->art_link_sel];
  int top = (int)l->line;
  int bottom = (int)(l->line + l->nlines);
  if (top < app->art_scroll) {
    app->art_scroll = top;
  } else if (app->art_rows > 0 && bottom > app->art_scroll + app->art_rows) {
    app->art_scroll = bottom - app->art_rows;
  }

  int max = (int)app->art_nlines - app->art_rows;
  if (max < 0) {
    max = 0;
  }
  if (app->art_scroll < 0) {
    app->art_scroll = 0;
  }
  if (app->art_scroll > max) {
    app->art_scroll = max;
  }
}

void article_link_move(App *app, int delta) {
  if (app->art_nlinks == 0) {
    ui_status(app, false, "No links in this article");
    return;
  }
  int n = (int)app->art_nlinks;
  int sel = app->art_link_sel;
  if (sel < 0) {
    sel = delta > 0 ? 0 : n - 1;
  } else {
    sel = (sel + delta) % n;
  }
  if (sel < 0) {
    sel += n;
  }
  app->art_link_sel = sel;
  article_link_reveal(app);
  ui_status(app, false, "Link %d/%d: %s", sel + 1, n, app->art_links[sel].url);
}

void article_open_link(App *app) {
  if (app->art_nlinks == 0) {
    ui_status(app, false, "No links in this article");
    return;
  }
  if (app->art_link_sel < 0 || (size_t)app->art_link_sel >= app->art_nlinks) {
    ui_status(app, false, "Select a link first (arrows)");
    return;
  }
  const char *url = app->art_links[app->art_link_sel].url;
  const char *browser = app->cfg->browser;

  /* Release the terminal so terminal browsers can take it over. */
  def_prog_mode();
  endwin();
  int rc = url_open(browser, url);
  reset_prog_mode();
  clearok(stdscr, TRUE);
  refresh();

  if (rc != 0) {
    ui_status(app, true, "Failed to launch browser: %s",
              browser ? browser : "");
  } else {
    ui_status(app, false, "Opened %s", url);
  }
}

void article_draw(App *app) {
  WINDOW *w = app->art_win;
  if (!w) {
    return;
  }
  werase(w);
  render_box(w, app->focus == PANE_ARTICLE);

  if (!app->article) {
    render_text(w, 1, 2, app->art_cols - 3, "(select a headline to read)",
                ATTR_READ);
    wnoutrefresh(w);
    return;
  }

  int sel_first = -1, sel_last = -1;
  if (app->art_link_sel >= 0 && (size_t)app->art_link_sel < app->art_nlinks) {
    ArtLink *l = &app->art_links[app->art_link_sel];
    sel_first = (int)l->line;
    sel_last = (int)(l->line + l->nlines) - 1;
  }

  for (int r = 0; r < app->art_rows; r++) {
    int idx = app->art_scroll + r;
    if (idx < 0 || (size_t)idx >= app->art_nlines) {
      break;
    }
    ArtLine *l = &app->art_lines[idx];
    int attr = l->attr;
    if (idx >= sel_first && idx <= sel_last) {
      attr = COLOR_PAIR(CP_SELECTED);
    }
    render_text(w, r + 1, 1, app->art_cols - 2, l->text, attr);
  }
  wnoutrefresh(w);
}
