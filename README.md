# tuituirss

A fast, keyboard-driven terminal client for the self-hosted
[Tiny Tiny RSS](https://tt-rss.org) reader. It talks to an existing tt-rss
server exclusively through the official JSON API, so no server-side changes are
needed beyond enabling API access for your user.

```
┌──────────────────────────────┬──────────────────────────────────────────────┐
│ Virtual feeds                │ Headlines                                    │
│  *Starred               (12) │ o   First headline title          Example Blog│
│  Published                   │ o*  Second headline                          │
│  All articles           (340)│  p  Third headline                           │
│ ── Labels ──                 │                                              │
│  @work                   (4) │                                              │
│ ── Feeds ──                  │                                              │
│  v News                      │                                              │
│    blog.example.com      (4) │                                              │
├──────────────────────────────┴──────────────────────────────────────────────┤
│ Article: title, feed, author, date, link, plain-text content, note, labels  │
├─────────────────────────────────────────────────────────────────────────────┤
│ Updated                                        12 unread  Starred  all_articles
└─────────────────────────────────────────────────────────────────────────────┘
```

## Features

- Three-pane layout: feeds/labels/categories, headline list, article reader.
- Read workflow: browse, read, toggle unread / starred / published, set notes
  and scores, assign labels, mark feeds or views read.
- Lightweight write workflow: subscribe / unsubscribe feeds, force a feed
  update.
- View modes: all, unread, adaptive, marked, updated, published, has note.
- Simple headline filtering and headline paging.
- Article link navigation: step through the URLs detected in a post and open
  the selected one in an external browser.
- Session reuse across restarts (the password is never stored).

## Dependencies

- A C11 compiler and `make`
- Development headers for:
  - `libcurl`
  - `jansson`
  - `ncursesw` (wide-character ncurses)
  - `libcrypto` (OpenSSL; used only for SHA-1 cache-key hashing)

On Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config libcurl4-openssl-dev \
    libjansson-dev libncurses-dev libssl-dev
```

## Build

```sh
make            # builds ./tuituirss
make test       # builds and runs the unit tests
make man        # view the tuituirss(1) man page
make install    # install the binary and man page (PREFIX=/usr/local)
```

### Packages

`make pkg` builds a `.deb` and an `.rpm` using
[nfpm](https://nfpm.goreleaser.com/) (which must be on `PATH`); `make deb` or
`make rpm` build just one. The version is read from `src/main.c` and the
architecture from the host (or from `dpkg --print-architecture` when
available).

## Configuration

Copy `tuituirssrc.example.json` to `~/.tuituirssrc.json` and edit it:

```json
{
  "server_url": "https://tt-rss.example.com/tt-rss/api/",
  "username": "your-user",
  "data_dir": "~/.tuituirss",
  "insecure": false,
  "ca_file": null,
  "browser": "xdg-open",
  "theme": "dark",
  "timeout_sec": 30,
  "debug": false,
  "log_file": null
}
```

| Field | Meaning |
|-------|---------|
| `server_url` | Full URL of the tt-rss `api/` endpoint |
| `username` | tt-rss login |
| `data_dir` | Where the session cache and logs live (default `~/.tuituirss`) |
| `insecure` | Skip TLS verification (self-signed certificates) |
| `ca_file` | Optional custom CA bundle |
| `browser` | Command used to open article links (default `$BROWSER`, else `xdg-open`; `open` on macOS). Arguments are allowed, e.g. `firefox --new-tab` |
| `theme` | Color theme: `dark` (default), `light`, or `mono` (no ANSI colors, attributes only). Override with `--theme`, `--dark`, `--light` or `--mono` |
| `timeout_sec` | Network timeout in seconds |
| `debug` | Write a debug log (credentials are redacted) |
| `log_file` | Log path; defaults to `data_dir/tuituirss.log` when `debug` is on |

A different config file can be selected with `-c/--config PATH`.

The password is **prompted for on startup** and never written to disk. For
scripting, set `TTUIRSS_PASSWORD` to skip the prompt.

> Make sure **Preferences → Enable API access** is turned on for your tt-rss
> user, otherwise login fails with `API_DISABLED`.

## Usage

```sh
./tuituirss
./tuituirss -c /path/to/config.json
./tuituirss --dark          # force the dark color theme
```

Use `j`/`k` to move, `Enter` to open, `h`/`l` or `Tab` to move between panes,
and `?` for the full keybinding list. In the article pane, the arrow keys step
through the detected links and `Enter` opens the selected one in your browser.
See [docs/keybindings.md](docs/keybindings.md).

## Testing and analysis

```sh
make test                 # unit tests (no network)
make asan                 # unit tests under AddressSanitizer + UBSan
make analyze              # GCC static analyzer (-fanalyzer)
make fuzz                 # libFuzzer harness for the parsers (needs clang)
./tests/run_tests.sh      # build + unit tests

# integration (needs a reachable tt-rss instance):
TTRSS_URL=https://host/tt-rss/api/ TTRSS_USER=you TTRSS_PASS=secret \
    RUN_INTEGRATION=1 ./tests/run_tests.sh
```

The integration test logs in, walks categories → feeds → headlines → article,
toggles and restores a headline's starred flag, reads counters and labels, then
logs out. `make fuzz` feeds arbitrary bytes to the HTML/UTF-8/JSON parsers and
the envelope decoder under ASan/UBSan; tune the duration with `FUZZ_TIME`.

## Releases

Pushing a `v*` tag (for example `v0.0.2`, matching `TUIIRSS_VERSION` in
`src/main.c`) triggers the
[release workflow](.github/workflows/release.yml), which builds `.deb` and
`.rpm` packages for amd64 and arm64 and attaches them to a GitHub release. The
workflow can also be dispatched manually from the Actions tab to produce
artifacts without creating a release.

## Security notes

The server response is semi-trusted and the feed/article strings inside it are
untrusted, so:

- **Terminal escape injection**: all displayed strings are sanitized. Control
  characters (C0/C1), DEL, bidi overrides/isolates and zero-width marks are
  removed — including code points smuggled in as HTML entities (`&#x1b;`,
  `&#0;`, `&#x202e;`). Invalid UTF-8 is dropped. Article HTML is converted to
  plain text, so nothing in a feed is rendered or executed.
- **External browser**: URLs are only ever handed to the configured `browser`
  command as an `execvp` argument (no shell), and only after you select one and
  press `Enter`. They are never interpolated into a shell command or terminal
  escape.
- **Response limits**: a single API response is capped (16 MiB, after
  decompression) to bound memory use and resist decompression bombs.
- **Transport**: TLS verification is on by default; redirects are not followed
  (so the POST body — which may contain the password — cannot be replayed to
  another host on 307/308) and the transport is restricted to the configured
  scheme.
- **Credentials**: the password is never persisted, is zeroed in memory, and is
  redacted in logs along with `sid`/`session_id`. Session files are created
  `0600` with `O_NOFOLLOW`; the data directory is verified to be a real
  directory owned by the user and tightened to `0700`.
- **Build hardening**: `-D_FORTIFY_SOURCE=2`, stack protector, stack-clash
  protection, `-fcf-protection`, `-Werror=format-security`, and
  RELRO/now + PIE are enabled by default.

Setting `insecure: true` disables TLS verification and should only be used for
self-signed certificates on trusted networks.

## Project layout

```
src/
  main.c        entry point, config/session/login, signal handling
  config/       JSON config loading
  api/          request building, libcurl transport, session cache
  model/        typed structs + jansson parsers
  ui/           ncurses views, dialogs, keybindings
  util/         strings, UTF-8, HTML→text, hashing, logging, autofree
tests/
  unit/         parser/model/config/envelope/session tests (no network)
  integration/  live-server API test
  fuzz/         libFuzzer harness for the untrusted-input parsers
docs/           keybindings, manual test notes, and the tuituirss(1) man page
```

See [PLAN.md](PLAN.md) for the full design and [docs/tuituirss.1](docs/tuituirss.1)
for the man page.

## License

MIT — see [LICENSE](LICENSE). This is an independent client that speaks the
tt-rss JSON API; it contains no tt-rss code and is not a derivative work of
tt-rss (which is GPL-3.0-or-later).
