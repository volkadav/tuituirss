// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

static WINDOW *centered(App *app, int h, int w, int *out_y, int *out_x) {
  if (w > app->cols - 4)
    w = app->cols - 4;
  if (h > app->rows - 2)
    h = app->rows - 2;
  if (w < 10)
    w = 10;
  if (h < 3)
    h = 3;
  int y = (app->rows - h) / 2;
  int x = (app->cols - w) / 2;
  if (out_y)
    *out_y = y;
  if (out_x)
    *out_x = x;
  WINDOW *win = newwin(h, w, y, x);
  keypad(win, TRUE);
  return win;
}

char *dialog_input(App *app, const char *title, const char *initial) {
  int h = 5, w = 64;
  WINDOW *win = centered(app, h, w, NULL, NULL);
  int wh, ww;
  getmaxyx(win, wh, ww);
  (void)wh;

  char buf[1024];
  snprintf(buf, sizeof buf, "%s", initial ? initial : "");
  size_t pos = strlen(buf);
  char *result = NULL;
  bool done = false;

  while (!done) {
    werase(win);
    box(win, 0, 0);
    render_text(win, 0, 2, ww - 3, title, COLOR_PAIR(CP_TITLE) | A_BOLD);
    render_text(win, 2, 2, ww - 4, buf, COLOR_PAIR(CP_DEFAULT));
    wmove(win, 2, 2 + (int)pos);
    wrefresh(win);

    int ch = wgetch(win);
    switch (ch) {
    case 27:
      done = true;
      break;
    case '\n':
    case KEY_ENTER:
      result = xstrdup(buf);
      done = true;
      break;
    case KEY_BACKSPACE:
    case 127:
    case 8:
      if (pos > 0) {
        memmove(buf + pos - 1, buf + pos, strlen(buf + pos) + 1);
        pos--;
      }
      break;
    case KEY_LEFT:
      if (pos > 0)
        pos--;
      break;
    case KEY_RIGHT:
      if (buf[pos])
        pos++;
      break;
    case KEY_HOME:
      pos = 0;
      break;
    case KEY_END:
      pos = strlen(buf);
      break;
    default:
      if (ch >= 32 && ch < 127 && strlen(buf) < sizeof(buf) - 1) {
        memmove(buf + pos + 1, buf + pos, strlen(buf + pos) + 1);
        buf[pos] = (char)ch;
        pos++;
      }
      break;
    }
  }
  delwin(win);
  return result;
}

int dialog_confirm(App *app, const char *title, const char *question) {
  int h = 5, w = 60;
  WINDOW *win = centered(app, h, w, NULL, NULL);
  int wh, ww;
  getmaxyx(win, wh, ww);
  (void)wh;

  werase(win);
  box(win, 0, 0);
  render_text(win, 0, 2, ww - 3, title, COLOR_PAIR(CP_TITLE) | A_BOLD);
  render_text(win, 2, 2, ww - 4, question, COLOR_PAIR(CP_DEFAULT));
  render_text(win, 3, 2, ww - 4, "[y/N]", COLOR_PAIR(CP_READ));
  wrefresh(win);

  int ch = wgetch(win);
  delwin(win);
  return ch == 'y' || ch == 'Y';
}

void dialog_message(App *app, const char *title, const char *msg) {
  int h = 7, w = 64;
  WINDOW *win = centered(app, h, w, NULL, NULL);
  int wh, ww;
  getmaxyx(win, wh, ww);
  (void)wh;

  werase(win);
  box(win, 0, 0);
  render_text(win, 0, 2, ww - 3, title, COLOR_PAIR(CP_TITLE) | A_BOLD);
  render_text(win, 2, 2, ww - 4, msg, COLOR_PAIR(CP_DEFAULT));
  render_text(win, h - 2, 2, ww - 4, "Press any key to close",
              COLOR_PAIR(CP_READ));
  wrefresh(win);
  wgetch(win);
  delwin(win);
}

int dialog_label_picker(App *app, const int *ids, size_t n) {
  Label *labels = NULL;
  size_t nl = 0;
  if (api_get_labels(app->api, ids[0], &labels, &nl) != 0) {
    ui_status(app, true, "Failed to load labels: %s",
              api_last_error_string(app->api));
    return -1;
  }
  if (nl == 0) {
    label_free(labels, nl);
    dialog_message(app, "Labels", "No labels defined on the server.");
    return 0;
  }

  int h = (int)nl + 4;
  if (h > app->rows - 2)
    h = app->rows - 2;
  WINDOW *win = centered(app, h, 56, NULL, NULL);
  int wh, ww;
  getmaxyx(win, wh, ww);
  int visible = wh - 3;

  int sel = 0, top = 0;
  bool done = false;
  int rc = 0;

  while (!done) {
    werase(win);
    box(win, 0, 0);
    render_text(win, 0, 2, ww - 3, "Labels  (space toggle, enter apply)",
                COLOR_PAIR(CP_TITLE) | A_BOLD);
    if (sel < top)
      top = sel;
    if (sel >= top + visible)
      top = sel - visible + 1;
    for (int r = 0; r < visible; r++) {
      int idx = top + r;
      if (idx < 0 || (size_t)idx >= nl)
        break;
      int attr = (idx == sel) ? COLOR_PAIR(CP_SELECTED) | A_BOLD
                              : COLOR_PAIR(CP_DEFAULT);
      if (idx == sel)
        render_fill(win, r + 1, 1, ww - 2, attr);
      char line[600];
      snprintf(line, sizeof line, "[%c] %s", labels[idx].checked ? 'x' : ' ',
               labels[idx].caption ? labels[idx].caption : "");
      render_text(win, r + 1, 2, ww - 4, line, attr);
    }
    wrefresh(win);

    int ch = wgetch(win);
    switch (ch) {
    case 27:
      rc = -1;
      done = true;
      break;
    case 'j':
    case KEY_DOWN:
      if ((size_t)(sel + 1) < nl)
        sel++;
      break;
    case 'k':
    case KEY_UP:
      if (sel > 0)
        sel--;
      break;
    case ' ':
      labels[sel].checked = !labels[sel].checked;
      break;
    case '\n':
    case KEY_ENTER: {
      for (size_t i = 0; i < nl; i++) {
        if (api_set_article_label(app->api, ids, n, labels[i].id,
                                  labels[i].checked) != 0) {
          ui_status(app, true, "Failed to set label: %s",
                    api_last_error_string(app->api));
          rc = -1;
          break;
        }
      }
      if (rc == 0)
        ui_status(app, false, "Labels updated");
      done = true;
      break;
    }
    default:
      break;
    }
  }
  delwin(win);
  label_free(labels, nl);
  return rc;
}

void dialog_help(App *app) {
  char *text = input_help_text();
  int h = app->rows - 4;
  if (h > 30)
    h = 30;
  if (h < 5)
    h = 5;
  int w = 64;
  WINDOW *win = centered(app, h, w, NULL, NULL);
  int wh, ww;
  getmaxyx(win, wh, ww);

  /* split into lines */
  size_t nlines = 0, cap = 0;
  char **lines = NULL;
  char *save = text;
  char *line;
  while ((line = strsep(&save, "\n")) != NULL) {
    if (nlines == cap) {
      cap = cap ? cap * 2 : 32;
      lines = xrealloc(lines, cap * sizeof(char *));
    }
    lines[nlines++] = line;
  }

  int top = 0;
  int visible = wh - 3;
  bool done = false;
  while (!done) {
    werase(win);
    box(win, 0, 0);
    render_text(win, 0, 2, ww - 3, "tuituirss keybindings",
                COLOR_PAIR(CP_TITLE) | A_BOLD);
    for (int r = 0; r < visible; r++) {
      int idx = top + r;
      if (idx < 0 || (size_t)idx >= nlines)
        break;
      render_text(win, r + 1, 2, ww - 4, lines[idx], COLOR_PAIR(CP_DEFAULT));
    }
    wrefresh(win);

    int ch = wgetch(win);
    switch (ch) {
    case 'j':
    case KEY_DOWN:
      if (top + visible < (int)nlines)
        top++;
      break;
    case 'k':
    case KEY_UP:
      if (top > 0)
        top--;
      break;
    case ' ':
    case KEY_NPAGE:
      top += visible;
      if (top > (int)nlines - visible)
        top = (int)nlines - visible;
      if (top < 0)
        top = 0;
      break;
    default:
      done = true;
      break;
    }
  }
  free(lines);
  free(text);
  delwin(win);
}
