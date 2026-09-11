# Keybindings

All bindings live in one table in `src/ui/input.c`, so this document and the
in-app `?` overlay are generated from the same source of truth.

Contexts: **global** (any pane), **sidebar**, **headlines**, **article**.

| Key | Context | Action |
|-----|---------|--------|
| `j` / `Down` | sidebar, headlines | move selection down |
| `k` / `Up` | sidebar, headlines | move selection up |
| `g` / `Home` | sidebar, headlines | first item |
| `G` / `End` | sidebar, headlines | last item (loads more headlines if needed) |
| `l` / `Tab` / `Right` | global | focus next pane |
| `h` / `Shift-Tab` / `Left` | global | focus previous pane |
| `Enter` | sidebar | open feed / category, load headlines |
| `Enter` | headlines | open the article |
| `Space` | headlines | load the selected article without changing focus |
| `j` / `k` / `PgDn` / `PgUp` | article | scroll the article |
| `r` | headline/article | toggle read / unread |
| `s` | headline/article | toggle starred (marked) |
| `p` | headline/article | toggle published |
| `n` | headlines | jump to the next unread headline |
| `x` | headlines | mark the current view read (`catchupFeed`) |
| `x` | sidebar | mark the selected feed/category read |
| `m` | headline/article | set a note (empty clears it) |
| `+` / `-` | headline/article | increase / decrease score |
| `L` | headline/article | open the label picker |
| `a` | global | cycle view mode (all / unread / adaptive / marked / updated / published / has note) |
| `/` | headlines | set a filter (search) for the current feed |
| `A` | sidebar | subscribe to a feed (prompts for URL) |
| `D` | sidebar | unsubscribe the selected feed |
| `u` | sidebar | force an update of the selected feed |
| `o` | global | toggle split / article-only layout |
| `R` | global | refresh the sidebar and current headlines |
| `?` | global | help overlay |
| `q` | global | go back one level; quit from the sidebar |
| `Esc` | global | go back one level |
