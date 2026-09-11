// SPDX-License-Identifier: MIT
#include "ui/ui.h"
#include "util/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int key;
    unsigned ctx;
    Action act;
    const char *desc;
} Binding;

static const Binding g_bindings[] = {
    { 'j', CTX_LIST, ACT_DOWN, "move selection down" },
    { KEY_DOWN, CTX_LIST, ACT_DOWN, NULL },
    { 'k', CTX_LIST, ACT_UP, "move selection up" },
    { KEY_UP, CTX_LIST, ACT_UP, NULL },
    { 'g', CTX_LIST, ACT_FIRST, "jump to first item" },
    { KEY_HOME, CTX_LIST, ACT_FIRST, NULL },
    { 'G', CTX_LIST, ACT_LAST, "jump to last item / load more" },
    { KEY_END, CTX_LIST, ACT_LAST, NULL },
    { 'l', CTX_GLOBAL, ACT_FOCUS_NEXT, "focus next pane" },
    { '\t', CTX_GLOBAL, ACT_FOCUS_NEXT, NULL },
    { KEY_RIGHT, CTX_GLOBAL, ACT_FOCUS_NEXT, NULL },
    { 'h', CTX_GLOBAL, ACT_FOCUS_PREV, "focus previous pane" },
    { KEY_BTAB, CTX_GLOBAL, ACT_FOCUS_PREV, NULL },
    { KEY_LEFT, CTX_GLOBAL, ACT_FOCUS_PREV, NULL },
    { '\n', CTX_LIST, ACT_OPEN, "open feed / article" },
    { KEY_ENTER, CTX_LIST, ACT_OPEN, NULL },
    { ' ', CTX_HEADLINES, ACT_TOGGLE_ARTICLE, "load into article pane" },
    { 'r', CTX_LIST, ACT_TOGGLE_READ, "toggle read/unread" },
    { 'r', CTX_ARTICLE, ACT_TOGGLE_READ, NULL },
    { 's', CTX_LIST, ACT_TOGGLE_STAR, "toggle starred" },
    { 's', CTX_ARTICLE, ACT_TOGGLE_STAR, NULL },
    { 'p', CTX_LIST, ACT_TOGGLE_PUBLISH, "toggle published" },
    { 'p', CTX_ARTICLE, ACT_TOGGLE_PUBLISH, NULL },
    { 'n', CTX_HEADLINES, ACT_NEXT_UNREAD, "next unread headline" },
    { 'x', CTX_HEADLINES, ACT_CATCHUP, "mark view read (catchup)" },
    { 'x', CTX_SIDEBAR, ACT_CATCHUP, "mark feed/category read" },
    { 'm', CTX_LIST, ACT_NOTE, "set note" },
    { 'm', CTX_ARTICLE, ACT_NOTE, NULL },
    { '+', CTX_LIST, ACT_SCORE_UP, "increase score" },
    { '+', CTX_ARTICLE, ACT_SCORE_UP, NULL },
    { '=', CTX_LIST, ACT_SCORE_UP, NULL },
    { '-', CTX_LIST, ACT_SCORE_DOWN, "decrease score" },
    { '-', CTX_ARTICLE, ACT_SCORE_DOWN, NULL },
    { 'L', CTX_LIST, ACT_LABELS, "assign labels" },
    { 'L', CTX_ARTICLE, ACT_LABELS, NULL },
    { 'a', CTX_GLOBAL, ACT_CYCLE_VIEW, "cycle view mode" },
    { '/', CTX_HEADLINES, ACT_FILTER, "filter headlines" },
    { 'A', CTX_SIDEBAR, ACT_SUBSCRIBE, "subscribe to feed" },
    { 'D', CTX_SIDEBAR, ACT_UNSUBSCRIBE, "unsubscribe feed" },
    { 'u', CTX_SIDEBAR, ACT_UPDATE_FEED, "update feed now" },
    { 'o', CTX_GLOBAL, ACT_TOGGLE_LAYOUT, "toggle split/article layout" },
    { 'R', CTX_GLOBAL, ACT_REFRESH, "refresh current view" },
    { '?', CTX_GLOBAL, ACT_HELP, "show this help" },
    { 'q', CTX_GLOBAL, ACT_QUIT, "back / quit" },
    { 27, CTX_GLOBAL, ACT_BACK, "back" },
    { KEY_NPAGE, CTX_ARTICLE, ACT_DOWN, NULL },
    { KEY_PPAGE, CTX_ARTICLE, ACT_UP, NULL },
    { KEY_NPAGE, CTX_HEADLINES, ACT_LOAD_MORE, NULL },
    { KEY_RESIZE, CTX_GLOBAL, ACT_REFRESH, NULL },
};

unsigned input_context(App *app)
{
    switch (app->focus) {
    case PANE_FEEDS:
        return CTX_SIDEBAR;
    case PANE_HEADLINES:
        return CTX_HEADLINES;
    case PANE_ARTICLE:
        return CTX_ARTICLE;
    }
    return CTX_SIDEBAR;
}

Action input_action(int ch, unsigned ctx)
{
    unsigned all = ctx | CTX_GLOBAL;
    for (size_t i = 0; i < sizeof g_bindings / sizeof g_bindings[0]; i++) {
        if (g_bindings[i].key == ch && (g_bindings[i].ctx & all))
            return g_bindings[i].act;
    }
    return ACT_NONE;
}

char *input_help_text(void)
{
    size_t cap = 4096;
    char *buf = xmalloc(cap);
    size_t len = 0;
    buf[0] = '\0';

    for (size_t i = 0; i < sizeof g_bindings / sizeof g_bindings[0]; i++) {
        if (!g_bindings[i].desc)
            continue;
        char key[16];
        int k = g_bindings[i].key;
        if (k == ' ')
            snprintf(key, sizeof key, "Space");
        else if (k >= 32 && k < 127)
            snprintf(key, sizeof key, "%c", k);
        else if (k == '\n')
            snprintf(key, sizeof key, "Enter");
        else if (k == '\t')
            snprintf(key, sizeof key, "Tab");
        else if (k == KEY_DOWN)
            snprintf(key, sizeof key, "Down");
        else if (k == KEY_UP)
            snprintf(key, sizeof key, "Up");
        else if (k == KEY_LEFT)
            snprintf(key, sizeof key, "Left");
        else if (k == KEY_RIGHT)
            snprintf(key, sizeof key, "Right");
        else if (k == KEY_HOME)
            snprintf(key, sizeof key, "Home");
        else if (k == KEY_END)
            snprintf(key, sizeof key, "End");
        else if (k == 27)
            snprintf(key, sizeof key, "Esc");
        else
            snprintf(key, sizeof key, "0x%x", k);
        int n = snprintf(buf + len, cap - len, "  %-8s %s\n", key,
                         g_bindings[i].desc);
        if (n < 0)
            break;
        len += (size_t)n;
        if (len + 128 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    return buf;
}
