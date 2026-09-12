// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  short fg;
  short bg;
} PairDef;

typedef struct {
  const char *name;
  bool mono;              /* skip color setup entirely (no ANSI colors) */
  PairDef pair[CP_COUNT]; /* foreground/background (ignored when mono) */
  int attr[CP_COUNT];     /* attributes folded into draws of that pair */
} Theme;

/* `-1` means "use the terminal's default" (works on both light and dark
 * backgrounds). Accent colors are chosen per background: bright on dark,
 * darker on light. CP_READ is rendered with A_DIM (see ATTR_READ). The "mono"
 * theme defines no colors and relies solely on attributes. */
static const Theme g_themes[] = {
    {"dark",
     false,
     {
         [CP_DEFAULT] = {-1, -1},
         [CP_SELECTED] = {COLOR_BLACK, COLOR_CYAN},
         [CP_UNREAD] = {-1, -1},
         [CP_READ] = {-1, -1},
         [CP_TITLE] = {COLOR_YELLOW, -1},
         [CP_STATUS] = {COLOR_BLACK, COLOR_CYAN},
         [CP_ERROR] = {COLOR_WHITE, COLOR_RED},
         [CP_FEED] = {COLOR_CYAN, -1},
         [CP_SECTION] = {COLOR_YELLOW, -1},
         [CP_STARRED] = {COLOR_YELLOW, -1},
         [CP_NOTE] = {COLOR_GREEN, -1},
         [CP_FOCUS] = {COLOR_CYAN, -1},
     },
     {0}},
    {"light",
     false,
     {
         [CP_DEFAULT] = {-1, -1},
         [CP_SELECTED] = {COLOR_BLACK, COLOR_CYAN},
         [CP_UNREAD] = {-1, -1},
         [CP_READ] = {-1, -1},
         [CP_TITLE] = {COLOR_BLUE, -1},
         [CP_STATUS] = {COLOR_BLACK, COLOR_CYAN},
         [CP_ERROR] = {COLOR_WHITE, COLOR_RED},
         [CP_FEED] = {COLOR_BLUE, -1},
         [CP_SECTION] = {COLOR_BLUE, -1},
         [CP_STARRED] = {COLOR_MAGENTA, -1},
         [CP_NOTE] = {COLOR_GREEN, -1},
         [CP_FOCUS] = {COLOR_BLUE, -1},
     },
     {0}},
    {"mono",
     true,
     {{-1, -1}},
     {
         [CP_SELECTED] = A_REVERSE | A_BOLD,
         [CP_STATUS] = A_REVERSE,
         [CP_ERROR] = A_REVERSE | A_BOLD,
         [CP_TITLE] = A_BOLD,
         [CP_SECTION] = A_BOLD,
         [CP_STARRED] = A_BOLD,
         [CP_FOCUS] = A_BOLD,
         [CP_READ] = A_DIM,
     }},
};

#define THEME_COUNT (sizeof g_themes / sizeof g_themes[0])
#define DEFAULT_THEME (&g_themes[0])

/* Attributes folded into a draw based on the color pair it references. */
static int g_theme_attr[CP_COUNT];

static int theme_attr(int attr) {
  int pair = PAIR_NUMBER(attr);
  if (pair > 0 && pair < CP_COUNT) {
    attr |= g_theme_attr[pair];
  }
  return attr;
}

static const Theme *theme_find(const char *name) {
  if (name && *name) {
    for (size_t i = 0; i < THEME_COUNT; i++) {
      if (strcmp(g_themes[i].name, name) == 0) {
        return &g_themes[i];
      }
    }
  }
  return DEFAULT_THEME;
}

bool render_theme_valid(const char *theme) {
  if (!theme || !*theme) {
    return false;
  }
  for (size_t i = 0; i < THEME_COUNT; i++) {
    if (strcmp(g_themes[i].name, theme) == 0) {
      return true;
    }
  }
  return false;
}

const char *render_init(const char *theme) {
  const Theme *t = theme_find(theme);

  for (int i = 0; i < CP_COUNT; i++) {
    g_theme_attr[i] = t->attr[i];
  }

  /* The mono theme emits no color sequences at all. */
  if (!t->mono) {
    start_color();
    use_default_colors();
    for (int i = 1; i < CP_COUNT; i++) {
      init_pair((short)i, t->pair[i].fg, t->pair[i].bg);
    }
  }
  return t->name;
}

void render_box(WINDOW *w, bool focused) {
  if (!w) {
    return;
  }
  if (focused) {
    int attr = theme_attr(COLOR_PAIR(CP_FOCUS) | A_BOLD);
    wattron(w, attr);
    box(w, 0, 0);
    wattroff(w, attr);
  } else {
    box(w, 0, 0);
  }
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
  if (!w || !s || maxw <= 0) {
    return;
  }
  int wy, wx;
  getmaxyx(w, wy, wx);
  if (y < 0 || y >= wy || x < 0 || x >= wx) {
    return;
  }
  int avail = wx - x;
  if (maxw > avail) {
    maxw = avail;
  }
  if (maxw <= 0) {
    return;
  }
  const char *t = truncate_cells(s, maxw);
  attr = theme_attr(attr);
  if (attr) {
    wattron(w, attr);
  }
  mvwaddstr(w, y, x, t);
  if (attr) {
    wattroff(w, attr);
  }
}

void render_text_justify(WINDOW *w, int y, int x, int maxw, const char *s,
                         int attr, bool right) {
  if (!w || !s || maxw <= 0) {
    return;
  }
  size_t width = utf8_width(s);
  int sx = x;
  if (right) {
    if ((int)width >= maxw) {
      sx = x;
    } else {
      sx = x + maxw - (int)width;
    }
  }
  render_text(w, y, sx, maxw, s, attr);
}

void render_fill(WINDOW *w, int y, int x, int n, int attr) {
  if (!w || n <= 0) {
    return;
  }
  int wy, wx;
  getmaxyx(w, wy, wx);
  if (y < 0 || y >= wy || x >= wx) {
    return;
  }
  if (x + n > wx) {
    n = wx - x;
  }
  if (n <= 0) {
    return;
  }
  attr = theme_attr(attr);
  if (attr) {
    wattron(w, attr);
  }
  mvwhline(w, y, x, ' ', n);
  if (attr) {
    wattroff(w, attr);
  }
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
  if (!text) {
    return;
  }
  if (width < 1) {
    width = 1;
  }

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
        if (*word == '\0') {
          continue;
        }
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
    if (!nl) {
      break;
    }
    p = nl + 1;
  }

  if (lcells > 0) {
    push_line(lines, n, cap, line, attr);
  }
}

void artline_free(ArtLine *lines, size_t n) {
  if (!lines) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    free(lines[i].text);
  }
  free(lines);
}
