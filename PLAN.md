# tuituirss — a terminal client for Tiny Tiny RSS

## Purpose

A single, fast TUI client for the self-hosted [Tiny Tiny RSS](https://tt-rss.org)
reader. It talks to an existing tt-rss server exclusively through the official
JSON API (the `api/` endpoint), so no server-side changes are required beyond
enabling API access for the user.

The reference server is a read-only checkout at `/var/www/html/tt-rss`
(current API level **23**, schema version **151**).

## Goals

- Full read workflow: browse categories/feeds, list headlines, read articles,
  toggle unread/starred/published, mark as read, add/remove labels.
- Lightweight write workflow: subscribe/unsubscribe feeds, set a note, set score.
- Comfortable keyboard-first navigation with a three-pane layout.
- Article reading aids: plain-text rendering plus a selectable list of the URLs
  detected in a post, opened in an external browser on demand.
- Accessible presentation: named color themes (`dark`, `light`, and a `mono`
  theme that emits no ANSI color at all).
- Single native binary, minimal runtime dependencies (libc, ncurses, libcurl, jansson, libcrypto — the latter already pulled in by libcurl/OpenSSL).
- Preserve privacy: no credentials logged; no telemetry.

## Non-goals (for v1)

- Feed *management* UI beyond subscribe/unsubscribe (no category CRUD, no OPML import).
- Full text search (the API supports `search`, but v1 exposes a simple filter only).
- Offline cache / local storage of articles.
- Plugins, multi-profile switching, or admin operations.
- Rendering inline images/HTML (articles shown as plain text with links).

## Technology decisions

| Concern        | Choice                                          | Rationale |
|----------------|-------------------------------------------------|-----------|
| Language       | C (C11)                                         | User preference; minimal deps, single binary |
| TUI library   | `ncursesw` (wide-char)                          | Standard, ubiquitous |
| HTTP          | `libcurl`                                       | Handles TLS, proxies, redirects, auth edge cases |
| JSON          | `jansson`                                       | Small, well-known C JSON library |
| Build         | `make` + `pkg-config`                           | Simple; no codegen; portable |
| Config        | JSON file at `~/.tuituirssrc.json` (path overridable with `-c/--config`) | Already have jansson; secrets file chmod 0600 |
| App data dir  | `~/.tuituirss/` (overridable via config `data_dir`) | Session cache, logs, recorded fixtures |
| Tests         | Unit tests via a tiny assertion header + integration tests against a local tt-rss instance | Keep CI simple |

Optional (decided later, no v1 dependency): CMake as an alternative build.

## The tt-rss JSON API (reference summary)

Everything below was read from `/var/www/html/tt-rss/classes/API.php` and is the
contract the client is built against.

### Transport

- Endpoint: `https://HOST/tt-rss/api/` (path prefix depends on install location;
  the client must take the full URL from config).
- Method: `POST`, header `Content-Type: application/json`.
- Request body is a single JSON object. The method name goes in `"op"`.
- Session: `login` returns a `session_id`; every subsequent request includes it
  as `"sid"`. The server may return `NOT_LOGGED_IN` on an expired session.
- Response envelope: `{"seq": N, "status": 0|1, "content": {...}}`.
  `status` is `0` (OK) or `1` (error). On error, `content.error` is a code like
  `LOGIN_ERROR`, `NOT_LOGGED_IN`, `API_DISABLED`, `UNKNOWN_METHOD`, etc.
- The server only serves API requests if the user has **"Enable API access"**
  (`ENABLE_API_ACCESS`) enabled in preferences. The client should surface a clear
  error when it gets `API_DISABLED`.

### Methods used

| op                | key request fields                                   | content returned |
|-------------------|------------------------------------------------------|------------------|
| `login`           | `user`, `password`                                   | `session_id`, `api_level`, `config` |
| `logout`          | —                                                    | `status` |
| `isLoggedIn`      | —                                                    | `status` |
| `getVersion`      | —                                                    | `version` |
| `getApiLevel`     | —                                                    | `level` |
| `getCategories`   | `unread_only`, `enable_nested`, `include_empty`      | list of categories |
| `getFeeds`        | `cat_id`, `unread_only`, `include_nested`, `limit`, `offset` | list of feeds |
| `getHeadlines`    | `feed_id`, `limit`, `skip`, `view_mode`, `is_cat`, `show_excerpt`, `show_content`, `include_attachments`, `since_id`, `include_nested`, `order_by`, `check_first_id`, `include_header` | list of headlines (optionally `[header, list]`) |
| `getArticle`      | `article_id`                                         | full article(s) with content |
| `updateArticle`   | `article_ids`, `mode`, `field`, `data`               | `status`, `updated` |
| `catchupFeed`     | `feed_id`, `is_cat`, `mode`, `search_query`, `search_lang` | `status` |
| `getCounters`     | —                                                    | global counters |
| `getUnread`       | `feed_id`, `is_cat`                                  | `unread` |
| `getLabels`       | `article_id` (optional)                              | label list with `checked` |
| `setArticleLabel` | `article_ids`, `label_id`, `assign`                  | `status`, `updated` |
| `updateFeed`      | `feed_id`                                            | `status` |
| `subscribeToFeed` | `feed_url`, `category_id`, `login`, `password`       | `status` |
| `unsubscribeFeed` | `feed_id`                                            | `status` |
| `getConfig`       | —                                                    | `config` |

### `updateArticle` semantics

- `field`: `0` marked, `1` published, `2` unread, `3` note, `4` score.
- `mode`: `0` set false, `1` set true, `2` toggle.
- `data`: used for note (string) and score (int).

### `view_mode` values

`all_articles`, `unread`, `adaptive`, `marked`, `updated`, `published`, `has_note`.

### Special IDs (from `classes/Feeds.php` and `include/functions.php`)

```
FEED_ARCHIVED = 0           CATEGORY_UNCATEGORIZED = 0
FEED_STARRED = -1           CATEGORY_SPECIAL = -1
FEED_PUBLISHED = -2         CATEGORY_LABELS = -2
FEED_FRESH = -3             CATEGORY_ALL_EXCEPT_VIRTUAL = -3
FEED_ALL = -4               CATEGORY_ALL = -4
FEED_RECENTLY_READ = -6
FEED_ERROR = -7
LABEL_BASE_INDEX = -1024
PLUGIN_FEED_BASE_INDEX = -128
```

These must be represented as constants in the client so feed vs. category vs.
label vs. plugin-feed logic stays clear. The sidebar composes:

1. Special virtual feeds (Starred, Published, Fresh, All articles, Recently read, Archived)
2. Labels (a sub-list)
3. Categories and their feeds (nested via `getFeedTree` or `getCategories` + `getFeeds` with `include_nested`)

## Architecture

Layered, strictly separated so the API and the UI can be tested independently.

```
+-------------------------------------------------------------+
|  main.c  (init, event loop, wiring, signal handling)        |
+-------------------------------------------------------------+
|  ui/     (ncurses views: feed list, headline list, article, |
|           status bar, dialogs, input/keybindings)           |
+-------------------------------------------------------------+
|  model/  (typed structs + jansson parsers: Category, Feed,  |
|           Headline, Article, Counter, Label)                |
+-------------------------------------------------------------+
|  api/    (request builder, HTTP via libcurl, session mgmt,  |
|           error mapping)                                    |
+-------------------------------------------------------------+
|  config/ (JSON file loading, CLI overrides, data-dir paths)  |
+-------------------------------------------------------------+
|  util/   (string/utf8 helpers, logging, timers, allocators) |
+-------------------------------------------------------------+
```

### Data flow

1. `ui` layer calls a synchronous `api_get_feeds(...)`-style function.
2. `api` serializes args to a JSON request (jansson), POSTs it via libcurl,
   parses the envelope, and on `status==1` maps `content.error` to a typed error.
3. `model` turns `content` JSON into C structs with owned strings.
4. `ui` renders structs and owns their lifetime (frees after use).

All network calls are **synchronous**, executed outside the ncurses draw path,
with a short spinner/status message during blocking operations. A small
worker-thread + message-queue abstraction is a later optimization, not v1.

### Session handling

- `login` → store `session_id` and `api_level`.
- Cache the session across runs under the data dir so a restart doesn't require
  re-entering the password; validate with `isLoggedIn` on startup and fall back
  to `login` on `NOT_LOGGED_IN`.
- Cache file is named by a hash of the server URL (e.g. `session-<sha1>.json`),
  written mode `0600`, so multiple servers can coexist.
- Never write the password to disk. The password is prompt-only for v1
  (optionally read from the `TTUIRSS_PASSWORD` env var to ease scripting).

## Project layout

```
tuituirss/
├── Makefile
├── PLAN.md
├── README.md
├── .gitignore
├── tuituirssrc.example.json
├── src/
│   ├── main.c
│   ├── config/
│   │   ├── config.h
│   │   └── config.c
│   ├── api/
│   │   ├── api.h
│   │   ├── api.c            # request building + HTTP + envelope handling
│   │   ├── session.h
│   │   └── session.c
│   ├── model/
│   │   ├── model.h          # struct definitions + constants (IDs above)
│   │   └── parse.c          # jansson -> structs
│   ├── ui/
│   │   ├── ui.h             # ncurses init/teardown, layout constants
│   │   ├── screen.c         # main loop, view switching, resize
│   │   ├── feedlist.c
│   │   ├── headlines.c
│   │   ├── article.c
│   │   ├── status.c
│   │   ├── dialog.c         # modal: label picker, subscribe, search prompt
│   │   ├── input.c          # key -> action binding tables
│   │   └── render.c         # shared drawing helpers (line wrapping, colors)
│   └── util/
│       ├── util.h
│       ├── util.c           # utf8, alloc wrappers, min/max, time fmt
│       └── log.c            # opt-in debug logging to file, never creds
├── tests/
│   ├── unit/                # parser + model tests (no server)
│   ├── integration/         # against a local tt-rss instance
│   └── run_tests.sh
└── docs/
    ├── keybindings.md
    ├── manual-testing.md
    └── tuituirss.1         # man page
```

## UI layout

```
┌──────────────────────────────┬──────────────────────────────────────────────┐
│ Feeds (sidebar)              │ Headlines                                    │
│                              │                                              │
│  ★ Starred               (12)│  ▸ [unread] First headline title    feed  2h │
│  Published                   │    [read]   Second headline                  │
│  Fresh                       │    [star]  Third headline                    │
│  All articles           (340)│                                              │
│  Archived                    │                                              │
│ ── Labels ──                 │                                              │
│  @work                       │                                              │
│ ── Categories ──             │                                              │
│  ▸ News                      │                                              │
│    blog.example.com      (4) │                                              │
├──────────────────────────────┴──────────────────────────────────────────────┤
│ Article view (bottom-right pane; toggles to full width with `o`)            │
│ Title, feed, author, date, link, content, note, attachments                 │
├──────────────────────────────────────────────────────────────────────────────┤
│ Status bar: view mode · filter · sync indicator · error/info messages       │
└──────────────────────────────────────────────────────────────────────────────┘
```

- Left pane: feeds/categories/labels with unread counts.
- Top-right: headline list (title, feed, age, unread/star markers).
- Bottom-right: article reader, shown by default in split view; `o` toggles
  between split and article-only.
- Bottom: one-line status bar.

Panes are drawn with ncurses windows; resizing re-computes the split ratios.

## Keybindings (draft)

| Key | Context | Action |
|-----|---------|--------|
| `j`/`k` | lists | move selection down/up |
| `g`/`G` | lists | first/last item |
| `h`/`l` or `Tab` | global | move focus between panes |
| `Enter` | feed | open feed / load headlines |
| `Enter` | feed category | toggle collapsed (`>`) / expanded (`v`) |
| `Enter` | headline | open article |
| `Space` | headline | toggle open in article pane |
| `r` | headline/article | toggle read/unread |
| `s` | headline/article | toggle starred (marked) |
| `p` | headline/article | toggle published |
| `n` | headline | next unread |
| `x` | headline | mark feed/category all read (`catchupFeed`) |
| `m` | headline/article | set note |
| `+`/`-` | headline/article | set score |
| `L` | headline/article | assign/unassign labels (modal) |
| `a` | global | toggle adaptive/unread/all view mode |
| `/` | headlines | filter/search prompt |
| `A` | sidebar | subscribe to feed (prompt for URL) |
| `D` | sidebar (on feed) | unsubscribe feed |
| `u` | sidebar (on feed) | force update feed (`updateFeed`) |
| `o` | article | toggle split/full article layout |
| `Up`/`Right` | article | select next detected link |
| `Down`/`Left` | article | select previous detected link |
| `Enter` | article | open selected link in browser |
| `R` | global | refresh current view |
| `q` | global | back / quit (quit at top level) |
| `?` | global | help overlay |

All bindings live in a data table in `ui/input.c` so they are easy to remap and
document in one place.

## Implementation phases

### Phase 0 — Scaffolding
- `Makefile` (detect libs via `pkg-config`, `-std=c11 -Wall -Wextra`).
- Directory skeleton, `util/` helpers, minimal `main.c` that opens a screen and
  quits cleanly. CI that runs `make`.

### Phase 1 — Config + HTTP + API client
- `config/`: load `~/.tuituirssrc.json` (overridable with `-c/--config`), fields
  `server_url`, `username`, `data_dir` (default `~/.tuituirss/`), TLS options
  (`insecure` for self-signed certs, `ca_file`), the external `browser` command,
  and the `theme` name.
- Password is prompt-only for v1 (`TTUIRSS_PASSWORD` env var accepted for scripting);
  never persisted.
- `api/`: `login`, `logout`, `isLoggedIn`, `getVersion`; envelope + error mapping.
- Unit tests for JSON envelope parsing, error mapping, and config parsing using
  canned responses.

### Phase 2 — Model layer
- `model/`: structs + parsers for Category, Feed, Headline, Article, Counter,
  Label; constants for special IDs.
- Unit tests parsing representative (recorded) API payloads.

### Phase 3 — Read-only UI
- `ui/`: three-pane layout, feed sidebar (via `getCategories` + `getFeeds`
  `include_nested`, plus special feeds/labels), headline list (`getHeadlines`),
  article view (`getArticle`), status bar, focus model, keybindings for
  navigation and `q`.
- Headline paging: `limit`/`skip`, with `limit` defaulting to the current
  headline-pane height; "load more" on `End`, and re-clamp the page size on
  `SIGWINCH` resize.
- This is the first end-to-end usable milestone.

### Phase 4 — Mutations
- `updateArticle` wiring for unread/star/publish/note/score; `catchupFeed`;
  `setArticleLabel` + label modal; live counter refresh after each mutation
  (via `getCounters`/`getUnread`).

### Phase 5 — Feed management
- `subscribeToFeed` / `unsubscribeFeed` / `updateFeed`; sidebar refresh.

### Phase 6 — Polish
- Search/filter (`/`), view-mode switching, `?` help overlay, unread marker
  consistency, resize handling, session reuse across restarts, README + docs.
- Article link list with arrow-key navigation and `Enter` to open in the
  external browser (suspending ncurses so terminal browsers can run).
- Color themes (`dark`, `light`, `mono`) selectable in config and via
  `--theme`/`--dark`/`--light`/`--mono`; `mono` disables ANSI color entirely.
- `tuituirss(1)` man page under `docs/`.

## Testing strategy

- **Unit tests** (no network/server): model parsers, ID classification
  (feed vs. cat vs. label), envelope/error mapping, config parsing, keybinding
  table sanity. A minimal `assert.h`-style harness keeps the dependency surface
  to zero; `make test` runs them.
- **Integration tests**: a script that spins up a tt-rss container (the checkout
  includes `docker-compose.yml`), creates a user with API access, and exercises
  login → getCategories → getFeeds → getHeadlines → updateArticle → logout.
  Marked `integration` so they can be skipped without Docker.
- **Manual test list** in `docs/` for layout, resize, and unicode behavior.

## Security & correctness notes

- Never log `password` or `sid`; `util/log.c` must redact credential keys.
- Session cache files are written mode `0600`; the password is never stored
  (prompt-only for v1).
- Respect `NOT_LOGGED_IN` by transparently re-login when a password is available.
- Validate all server-supplied strings before display: lengths, embedded control
  chars (strip `\x00`, `\r`), and treat article content as untrusted HTML that
  must be converted to plain text. URLs are only ever handed to the configured
  browser as an `execvp` argument (no shell), and only after the user selects
  them explicitly; they are never interpolated into a shell command or a
  terminal escape.
- Handle `SIGWINCH` for resize (recompute split ratios and re-clamp page size)
  and `SIGINT`/`SIGTERM` to restore the terminal.

## Decisions

1. **Config format** — JSON at `~/.tuituirssrc.json`, overridable with
   `-c/--config`.
2. **Password** — prompt-only for v1 (with `TTUIRSS_PASSWORD` env var escape hatch).
3. **Data dir & multi-server session cache** — app data lives in `~/.tuituirss/`
   (overridable via config `data_dir`); the session cache file is named by a hash
   of the server URL.
4. **Headline paging** — `limit`/`skip`; default page size = current headline-pane
   height, re-clamped on terminal resize.
5. **External browser** — article links open through a configurable `browser`
   command (default `$BROWSER`, else `xdg-open`, or `open` on macOS). The command
   is split on whitespace and `exec`'d directly (no shell) with the URL appended;
   ncurses is suspended while it runs so terminal browsers work.
6. **Color themes** — named `dark` (default), `light`, and `mono`. `mono` skips
   color initialization entirely and distinguishes states with attributes
   (reverse, bold, dim, underline) only.
7. **License** — MIT. tuituirss is an independent client that talks to tt-rss
   over its JSON API and shares no tt-rss code, so tt-rss's GPL-3.0-or-later
   does not apply to it.
