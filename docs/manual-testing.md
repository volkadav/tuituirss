# Manual test checklist

Things that are hard to assert automatically (layout, resize, terminal
behaviour). Run against a live or mock tt-rss server.

## Layout

- [ ] Sidebar, headline list and article pane render with borders and fit the
      terminal at 80×24.
- [ ] At a very narrow width (< 60 columns) the layout does not crash and the
      panes remain usable.
- [ ] `o` toggles between the split layout (headlines + article) and
      article-only (article fills the right column).
- [ ] Focusing panes with `h`/`l`/`Tab` shows the selection in the expected
      pane; the cycle wraps around.

## Resize

- [ ] Resizing the terminal re-computes the split ratios without leaving
      artefacts (SIGWINCH).
- [ ] The headline page size follows the headline pane height; `G` loads more.
- [ ] Resizing with an article open re-wraps the text to the new width.
- [ ] Shrinking below the minimum size does not crash.

## Unicode / wide characters

- [ ] A feed titled with CJK or emoji renders without splitting code points.
- [ ] Headlines are truncated at the pane edge on a character boundary.
- [ ] Article bodies containing emoji/combining marks wrap sensibly.

## Read workflow

- [ ] `Enter` on a feed loads headlines; `Enter` on a headline opens it.
- [ ] `Space` loads an article without moving focus.
- [ ] `r`/`s`/`p` update the headline markers and the sidebar unread count.
- [ ] `n` jumps to the next unread headline.
- [ ] `x` marks a feed/category or the current view read.
- [ ] `m`, `+`/`-`, `L` update the note, score and labels.
- [ ] `a` cycles view modes and reloads headlines.
- [ ] `/` filters the current feed; an empty filter clears it.

## Feeds

- [ ] `A` subscribes to a new URL and refreshes the sidebar.
- [ ] `D` asks for confirmation and unsubscribes.
- [ ] `u` requests a feed update.

## Session / auth

- [ ] First run prompts for the password; a second run reuses the cached
      session without prompting.
- [ ] Deleting `data_dir/session-*.json` forces a fresh login.
- [ ] A server with API access disabled shows the `API_DISABLED` hint.
- [ ] `TTUIRSS_PASSWORD` suppresses the prompt.

## Terminal hygiene

- [ ] `q` from the sidebar exits and restores the terminal.
- [ ] `Ctrl-C` exits and restores the terminal.
- [ ] No credentials appear in the debug log.

## Security

- [ ] A headline or article containing `&#x1b;[31m` (entity-encoded ESC) shows
      the literal text and does not change colours or the window title.
- [ ] Bidi override characters (`&#x202e;`) in a title do not reverse the text.
- [ ] A very large feed response is rejected with a clear error rather than
      exhausting memory.
- [ ] Pointing `server_url` at an endpoint that redirects fails with the
      "set server_url to the final endpoint" message instead of following it.
- [ ] A pre-existing `data_dir` with `0755` permissions is tightened to `0700`
      on startup.
- [ ] A `data_dir` that is a symlink is refused with a warning.
- [ ] A symlink planted at the session-cache path is not written through.
