// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

void feedlist_free(App *app) {
  if (!app->items) {
    return;
  }
  for (size_t i = 0; i < app->nitems; i++) {
    free(app->items[i].title);
  }
  free(app->items);
  app->items = NULL;
  app->nitems = 0;
  app->items_cap = 0;
}

static void add_item(App *app, SidebarKind kind, SidebarSource source, int id,
                     const char *title, int unread, int depth, bool is_cat) {
  if (app->nitems == app->items_cap) {
    app->items_cap = app->items_cap ? app->items_cap * 2 : 64;
    app->items = xrealloc(app->items, app->items_cap * sizeof(SidebarItem));
  }
  SidebarItem *it = &app->items[app->nitems++];
  memset(it, 0, sizeof *it);
  it->kind = kind;
  it->source = source;
  it->id = id;
  it->title = xstrdup(title ? title : "");
  it->unread = unread;
  it->depth = depth;
  it->is_cat = is_cat;
  it->expanded = true;
}

static void add_section(App *app, const char *title) {
  add_item(app, SI_SECTION, SRC_VIRTUAL, 0, title, 0, 0, false);
}

int feedlist_load(App *app) {
  feedlist_free(app);

  Category *cats = NULL;
  size_t ncats = 0;
  Feed *feeds = NULL;
  size_t nfeeds = 0;

  if (api_get_categories(app->api, false, true, &cats, &ncats) != 0) {
    ui_status(app, true, "Failed to load categories: %s",
              api_last_error_string(app->api));
    category_free(cats, ncats);
    return -1;
  }
  if (api_get_feeds(app->api, CATEGORY_ALL, false, false, &feeds, &nfeeds) !=
      0) {
    ui_status(app, true, "Failed to load feeds: %s",
              api_last_error_string(app->api));
    category_free(cats, ncats);
    feed_free(feeds, nfeeds);
    return -1;
  }

  /* Virtual feeds first (the server returns them with cat_id == SPECIAL). */
  bool any_virtual = false;
  for (size_t i = 0; i < nfeeds; i++) {
    if (feeds[i].cat_id == CATEGORY_SPECIAL ||
        (feeds[i].cat_id == 0 && model_is_virtual_feed(feeds[i].id))) {
      if (!any_virtual) {
        add_section(app, "Virtual feeds");
        any_virtual = true;
      }
      add_item(app, SI_ITEM, SRC_VIRTUAL, feeds[i].id, feeds[i].title,
               feeds[i].unread, 0, false);
    }
  }

  /* Labels. */
  bool any_labels = false;
  for (size_t i = 0; i < nfeeds; i++) {
    if (feeds[i].cat_id == CATEGORY_LABELS) {
      if (!any_labels) {
        add_section(app, "Labels");
        any_labels = true;
      }
      add_item(app, SI_ITEM, SRC_LABEL, feeds[i].id, feeds[i].title,
               feeds[i].unread, 0, false);
    }
  }

  /* Categories and their feeds. */
  add_section(app, "Feeds");
  for (size_t c = 0; c < ncats; c++) {
    if (cats[c].id < 0) {
      continue;
    } /* labels / special handled above */
    add_item(app, SI_ITEM, SRC_CATEGORY, cats[c].id, cats[c].title,
             cats[c].unread, 0, true);
    for (size_t f = 0; f < nfeeds; f++) {
      if (feeds[f].cat_id == cats[c].id && feeds[f].id > 0 &&
          !feeds[f].is_cat) {
        add_item(app, SI_ITEM, SRC_FEED, feeds[f].id, feeds[f].title,
                 feeds[f].unread, 1, false);
      }
    }
  }

  category_free(cats, ncats);
  feed_free(feeds, nfeeds);

  /* Select the first selectable item. */
  app->side_sel = -1;
  for (size_t i = 0; i < app->nitems; i++) {
    if (app->items[i].kind == SI_ITEM) {
      app->side_sel = (int)i;
      break;
    }
  }
  app->side_top = 0;
  ui_status(app, false, "Loaded %zu feed(s)", nfeeds);
  return 0;
}

SidebarItem *feedlist_current(App *app) {
  if (app->side_sel < 0 || (size_t)app->side_sel >= app->nitems) {
    return NULL;
  }
  return &app->items[app->side_sel];
}

/* A depth>0 item is hidden when its owning category is collapsed. */
static bool item_visible(const App *app, size_t idx) {
  const SidebarItem *it = &app->items[idx];
  if (it->depth <= 0) {
    return true;
  }
  for (size_t j = idx; j-- > 0;) {
    const SidebarItem *p = &app->items[j];
    if (p->depth == 0) {
      return p->is_cat ? p->expanded : true;
    }
  }
  return true;
}

/* Visible row position of an item index (sections included). */
static int visible_pos(const App *app, int idx) {
  int v = 0;
  for (int i = 0; i < idx && (size_t)i < app->nitems; i++) {
    if (item_visible(app, (size_t)i)) {
      v++;
    }
  }
  return v;
}

/* Item index at a given visible row position. */
static int item_at_visible(const App *app, int vpos) {
  int v = 0;
  for (size_t i = 0; i < app->nitems; i++) {
    if (!item_visible(app, i)) {
      continue;
    }
    if (v == vpos) {
      return (int)i;
    }
    v++;
  }
  return (int)app->nitems - 1;
}

static void clamp_top(App *app) {
  if (app->side_rows <= 0) {
    return;
  }
  int sel = visible_pos(app, app->side_sel);
  int top = visible_pos(app, app->side_top);
  if (sel < top) {
    app->side_top = app->side_sel;
  } else if (sel >= top + app->side_rows) {
    app->side_top = item_at_visible(app, sel - app->side_rows + 1);
  }
}

static int next_selectable(App *app, int from, int dir) {
  int i = from;
  while (i >= 0 && (size_t)i < app->nitems) {
    if (app->items[i].kind == SI_ITEM && item_visible(app, (size_t)i)) {
      return i;
    }
    i += dir;
  }
  return -1;
}

void feedlist_move(App *app, int delta) {
  if (app->nitems == 0) {
    return;
  }
  int dir = delta > 0 ? 1 : -1;
  int count = delta > 0 ? delta : -delta;
  int idx = app->side_sel;
  for (int n = 0; n < count; n++) {
    int next = next_selectable(app, idx + dir, dir);
    if (next < 0) {
      break;
    }
    idx = next;
  }
  if (idx < 0) {
    return;
  }
  app->side_sel = idx;
  clamp_top(app);
}

void feedlist_home(App *app) {
  int first = next_selectable(app, 0, 1);
  if (first >= 0) {
    app->side_sel = first;
  }
  app->side_top = 0;
}

void feedlist_end(App *app) {
  int last = next_selectable(app, (int)app->nitems - 1, -1);
  if (last >= 0) {
    app->side_sel = last;
  }
  clamp_top(app);
}

void feedlist_draw(App *app) {
  WINDOW *w = app->feed_win;
  if (!w) {
    return;
  }
  werase(w);
  render_box(w, app->focus == PANE_FEEDS);
  int rows = app->side_rows;

  int r = 0;
  for (size_t idx = (size_t)(app->side_top > 0 ? app->side_top : 0);
       idx < app->nitems && r < rows; idx++) {
    if (!item_visible(app, idx)) {
      continue;
    }
    SidebarItem *it = &app->items[idx];
    int y = r + 1;
    bool selected = ((int)idx == app->side_sel) && it->kind == SI_ITEM;

    if (it->kind == SI_SECTION) {
      render_text(w, y, 2, app->feed_w - 3, it->title,
                  COLOR_PAIR(CP_SECTION) | A_BOLD);
      r++;
      continue;
    }

    int attr = COLOR_PAIR(CP_DEFAULT);
    if (selected) {
      attr = COLOR_PAIR(CP_SELECTED) | A_BOLD;
    } else if (it->unread > 0) {
      attr = COLOR_PAIR(CP_UNREAD) | A_BOLD;
    } else {
      attr = ATTR_READ;
    }

    if (selected) {
      render_fill(w, y, 1, app->feed_w - 2, attr);
    }

    char label[512];
    const char *mark = "";
    switch (it->source) {
    case SRC_VIRTUAL:
      if (it->id == FEED_STARRED) {
        mark = "*";
      }
      break;
    case SRC_LABEL:
      mark = "@";
      break;
    case SRC_CATEGORY:
      mark = it->expanded ? "v" : ">";
      break;
    default:
      break;
    }
    if (it->depth > 0) {
      snprintf(label, sizeof label, "%*s%s%s", it->depth * 2, "", mark,
               it->title);
    } else {
      snprintf(label, sizeof label, "%s%s", mark, it->title);
    }

    render_text(w, y, 2, app->feed_w - 4, label, attr);

    if (it->unread > 0) {
      char cnt[32];
      snprintf(cnt, sizeof cnt, "(%d)", it->unread);
      render_text_justify(w, y, 2, app->feed_w - 4, cnt, attr, true);
    }
    r++;
  }
  wnoutrefresh(w);
}

int feedlist_activate(App *app) {
  SidebarItem *it = feedlist_current(app);
  if (!it) {
    return -1;
  }
  if (it->is_cat) {
    it->expanded = !it->expanded;
    return 0;
  }
  int rc = headlines_load(app, it->id, it->is_cat, it->title);
  if (rc == 0) {
    app->focus = PANE_HEADLINES;
  }
  return rc;
}
