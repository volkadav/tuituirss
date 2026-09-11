// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

void render_init(void) {
  start_color();
  use_default_colors();

  init_pair(CP_DEFAULT, COLOR_WHITE, -1);
  init_pair(CP_SELECTED, COLOR_BLACK, COLOR_CYAN);
  init_pair(CP_UNREAD, COLOR_WHITE, -1);
  init_pair(CP_READ, COLOR_BLACK, -1);
  init_pair(CP_TITLE, COLOR_YELLOW, -1);
  init_pair(CP_STATUS, COLOR_BLACK, COLOR_CYAN);
  init_pair(CP_ERROR, COLOR_WHITE, COLOR_RED);
  init_pair(CP_FEED, COLOR_CYAN, -1);
  init_pair(CP_SECTION, COLOR_YELLOW, -1);
  init_pair(CP_STARRED, COLOR_YELLOW, -1);
  init_pair(CP_NOTE, COLOR_GREEN, -1);
}

static char *g_trunc;
static size_t g_trunc_cap;

static const char *truncate_cells(const char *s, int maxw) {
  size_t need = (size_t)maxw * 4 + 1;
  if (need > g_trunc_cap) {
    g_trunc = xrealloc(g_trunc, need);
    g_trunc_cap = need;
  }
  utf8_copy_cells(g_trunc, g_trunc_cap, s, (size_t)maxw);
  return g_trunc;
}

void render_text(WINDOW *w, int y, int x, int maxw, const char *s, int attr) {
  if (!w || !s || maxw <= 0)
    return;
  int wy, wx;
  getmaxyx(w, wy, wx);
  if (y < 0 || y >= wy || x < 0 || x >= wx)
    return;
  int avail = wx - x;
  if (maxw > avail)
    maxw = avail;
  if (maxw <= 0)
    return;
  const char *t = truncate_cells(s, maxw);
  if (attr)
    wattron(w, attr);
  mvwaddstr(w, y, x, t);
  if (attr)
    wattroff(w, attr);
}

void render_text_justify(WINDOW *w, int y, int x, int maxw, const char *s,
                         int attr, bool right) {
  if (!w || !s || maxw <= 0)
    return;
  size_t width = utf8_width(s);
  int sx = x;
  if (right) {
    if ((int)width >= maxw)
      sx = x;
    else
      sx = x + maxw - (int)width;
  }
  render_text(w, y, sx, maxw, s, attr);
}

void render_fill(WINDOW *w, int y, int x, int n, int attr) {
  if (!w || n <= 0)
    return;
  int wy, wx;
  getmaxyx(w, wy, wx);
  if (y < 0 || y >= wy || x >= wx)
    return;
  if (x + n > wx)
    n = wx - x;
  if (n <= 0)
    return;
  if (attr)
    wattron(w, attr);
  mvwhline(w, y, x, ' ', n);
  if (attr)
    wattroff(w, attr);
}

static void push_line(ArtLine **lines, size_t *n, size_t *cap, const char *s,
                      int attr) {
  if (*n == *cap) {
    *cap = *cap ? *cap * 2 : 32;
    *lines = xrealloc(*lines, *cap * sizeof(ArtLine));
  }
  (*lines)[*n].text = xstrdup(s);
  (*lines)[*n].attr = attr;
  (*n)++;
}

void render_wrap(const char *text, int width, int attr, ArtLine **lines,
                 size_t *n, size_t *cap) {
  if (!text)
    return;
  if (width < 1)
    width = 1;

  autofree char *line = xmalloc((size_t)width * 4 + 1);
  size_t lcells = 0;
  line[0] = '\0';

  const char *p = text;
  while (true) {
    /* extract one paragraph (up to newline) */
    const char *nl = strchr(p, '\n');
    size_t plen = nl ? (size_t)(nl - p) : strlen(p);
    autofree char *para = xstrndup(p, plen);

    if (plen == 0) {
      if (lcells > 0) {
        push_line(lines, n, cap, line, attr);
        line[0] = '\0';
        lcells = 0;
      }
      push_line(lines, n, cap, "", attr);
    } else {
      char *save = para;
      char *word;
      while ((word = strsep(&save, " \t")) != NULL) {
        if (*word == '\0')
          continue;
        size_t ww = utf8_width(word);
        if (lcells > 0 && lcells + 1 + ww <= (size_t)width) {
          strcat(line, " ");
          strcat(line, word);
          lcells += 1 + ww;
          continue;
        }
        if (lcells > 0) {
          push_line(lines, n, cap, line, attr);
          line[0] = '\0';
          lcells = 0;
        }
        while (ww > (size_t)width) {
          char tmp[4096];
          utf8_copy_cells(tmp, sizeof tmp, word, (size_t)width);
          push_line(lines, n, cap, tmp, attr);
          size_t used = strlen(tmp);
          word += used;
          ww = utf8_width(word);
        }
        strcpy(line, word);
        lcells = ww;
      }
    }
    if (!nl)
      break;
    p = nl + 1;
  }

  if (lcells > 0)
    push_line(lines, n, cap, line, attr);
}

void artline_free(ArtLine *lines, size_t n) {
  if (!lines)
    return;
  for (size_t i = 0; i < n; i++)
    free(lines[i].text);
  free(lines);
}
