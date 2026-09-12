// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdio.h>
#include <string.h>

void status_draw(App *app) {
  WINDOW *w = app->status_win;
  if (!w) {
    return;
  }
  int attr = app->status_error ? COLOR_PAIR(CP_ERROR) : COLOR_PAIR(CP_STATUS);
  werase(w);
  wbkgd(w, attr);
  render_fill(w, 0, 0, app->cols, attr);

  const char *left = app->status[0] ? app->status : "";
  render_text(w, 0, 0, app->cols - 1, left, attr);

  char right[512];
  const char *mode = app->view_mode ? app->view_mode : "";
  const char *title = app->cur_title ? app->cur_title : "";
  char unread[32] = "";
  if (app->counters.unread > 0) {
    snprintf(unread, sizeof unread, "%d unread  ", app->counters.unread);
  }
  if (app->filter && *app->filter) {
    snprintf(right, sizeof right, "%s%s [/ %s]  %s", unread, title, app->filter,
             mode);
  } else {
    snprintf(right, sizeof right, "%s%s  %s", unread, title, mode);
  }
  render_text_justify(w, 0, 0, app->cols - 1, right, attr, true);

  wnoutrefresh(w);
}
