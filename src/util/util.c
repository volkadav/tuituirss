// SPDX-License-Identifier: MIT
#include "util/util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

/* ----------------------------------------------------------------------- */
/* allocation                                                              */
/* ----------------------------------------------------------------------- */

static void oom(void)
{
    fputs("tuituirss: out of memory\n", stderr);
    abort();
}

void *xmalloc(size_t n)
{
    if (n == 0)
        n = 1;
    void *p = malloc(n);
    if (!p)
        oom();
    return p;
}

void *xcalloc(size_t n, size_t sz)
{
    if (n == 0 || sz == 0) {
        n = 1;
        sz = 1;
    }
    void *p = calloc(n, sz);
    if (!p)
        oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    if (n == 0)
        n = 1;
    void *q = realloc(p, n);
    if (!q)
        oom();
    return q;
}

char *xstrdup(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    if (!s)
        return NULL;
    size_t len = strnlen(s, n);
    char *p = xmalloc(len + 1);
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return xstrdup("");
    }
    char *out = xmalloc((size_t)n + 1);
    vsnprintf(out, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

/* ----------------------------------------------------------------------- */
/* strings                                                                 */
/* ----------------------------------------------------------------------- */

/* Defined in the UTF-8 section further down. */
static size_t utf8_decode(const unsigned char *s, size_t remaining,
                          uint32_t *cp);

/* Code points that must never reach the terminal: C0/C1 controls, DEL,
 * bidi embedding/override/isolate marks, and invisible/zero-width marks.
 * Blocking these prevents escape injection and visual spoofing via untrusted
 * feed content. */
static bool is_forbidden_cp(uint32_t cp)
{
    if (cp < 0x20 || cp == 0x7f)
        return true; /* C0 controls and DEL */
    if (cp >= 0x80 && cp <= 0x9f)
        return true; /* C1 controls (some terminals treat as escapes) */
    if (cp == 0x061c)
        return true; /* Arabic letter mark */
    if (cp == 0x200b || cp == 0x200c || cp == 0x200d)
        return true; /* zero-width space / joiners */
    if (cp == 0x200e || cp == 0x200f)
        return true; /* LRM / RLM */
    if (cp >= 0x202a && cp <= 0x202e)
        return true; /* bidi embedding / override */
    if (cp >= 0x2060 && cp <= 0x2064)
        return true; /* invisible operators */
    if (cp >= 0x2066 && cp <= 0x2069)
        return true; /* bidi isolates */
    if (cp == 0xfeff)
        return true; /* BOM / zero-width no-break space */
    return false;
}

/* Remove control/invisible code points in place. When keep_newlines is true
 * (article bodies) '\n' is preserved; otherwise newlines become spaces so a
 * single-line field stays single-line. Invalid UTF-8 is dropped. */
static void sanitize_inplace(char *s, bool keep_newlines)
{
    if (!s)
        return;
    unsigned char *r = (unsigned char *)s;
    unsigned char *w = r;
    size_t remaining = strlen(s);

    while (remaining > 0) {
        unsigned char c = *r;
        if (c < 0x80) {
            if (c == '\n') {
                *w++ = keep_newlines ? '\n' : ' ';
            } else if (c == '\r') {
                if (!keep_newlines)
                    *w++ = ' ';
            } else if (c == '\t' || (c >= 0x20 && c != 0x7f)) {
                *w++ = c;
            }
            r++;
            remaining--;
            continue;
        }
        uint32_t cp;
        size_t n = utf8_decode(r, remaining, &cp);
        if (n == 1 && cp == 0xFFFD) {
            r++; /* drop an invalid sequence one byte at a time */
            remaining--;
            continue;
        }
        if (!is_forbidden_cp(cp)) {
            memmove(w, r, n);
            w += n;
        }
        r += n;
        remaining -= n;
    }
    *w = '\0';
}

void str_strip_ctrl(char *s)
{
    sanitize_inplace(s, false);
}

char *str_trim(char *s)
{
    if (!s)
        return s;
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    return s;
}

const char *str_icontains(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    if (!*needle)
        return haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0)
            return p;
    }
    return NULL;
}

char *str_sanitize_copy(const char *s)
{
    if (!s)
        return NULL;
    char *copy = xstrdup(s);
    str_strip_ctrl(copy);
    return copy;
}

/* ---- HTML ------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} strbuf;

static void sb_put(strbuf *sb, char c)
{
    if (sb->len + 2 > sb->cap) {
        sb->cap = sb->cap ? sb->cap * 2 : 256;
        sb->buf = xrealloc(sb->buf, sb->cap);
    }
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

static void sb_put_cp(strbuf *sb, uint32_t cp)
{
    /* Never emit NUL, surrogates, out-of-range values, or control/invisible
     * code points (e.g. from &#0; or &#x1b;): substitute U+FFFD. */
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
        is_forbidden_cp(cp))
        cp = 0xFFFD;

    if (cp < 0x80) {
        sb_put(sb, (char)cp);
    } else if (cp < 0x800) {
        sb_put(sb, (char)(0xC0 | (cp >> 6)));
        sb_put(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        sb_put(sb, (char)(0xE0 | (cp >> 12)));
        sb_put(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_put(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_put(sb, (char)(0xF0 | (cp >> 18)));
        sb_put(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_put(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_put(sb, (char)(0x80 | (cp & 0x3F)));
    }
}

static const struct {
    const char *name;
    uint32_t cp;
} g_entities[] = {
    { "amp", '&' },       { "lt", '<' },        { "gt", '>' },
    { "quot", '"' },      { "apos", '\'' },     { "nbsp", 0x00A0 },
    { "copy", 0x00A9 },   { "reg", 0x00AE },    { "trade", 0x2122 },
    { "deg", 0x00B0 },    { "middot", 0x00B7 }, { "bull", 0x2022 },
    { "hellip", 0x2026 }, { "mdash", 0x2014 },  { "ndash", 0x2013 },
    { "lsquo", 0x2018 },  { "rsquo", 0x2019 },  { "ldquo", 0x201C },
    { "rdquo", 0x201D },  { "laquo", 0x00AB },  { "raquo", 0x00BB },
    { "times", 0x00D7 },  { "divide", 0x00F7 }, { "euro", 0x20AC },
    { "pound", 0x00A3 },  { "yen", 0x00A5 },    { "cent", 0x00A2 },
    { "sect", 0x00A7 },   { "para", 0x00B6 },   { "dagger", 0x2020 },
    { NULL, 0 }
};

static uint32_t entity_lookup(const char *name, size_t len)
{
    for (int i = 0; g_entities[i].name; i++) {
        if (strlen(g_entities[i].name) == len &&
            strncmp(g_entities[i].name, name, len) == 0)
            return g_entities[i].cp;
    }
    return 0xFFFD;
}

/* Decode a single entity starting at p (after '&'). Stores the number of
 * bytes consumed (including the '&' and ';') in *consumed. */
static uint32_t decode_entity(const char *p, size_t *consumed)
{
    *consumed = 1; /* just '&' */
    const char *semi = strchr(p, ';');
    if (!semi || (size_t)(semi - p) > 12)
        return '&';
    if (p[0] == '#') {
        uint32_t cp = 0;
        if (p[1] == 'x' || p[1] == 'X') {
            for (const char *q = p + 2; q < semi; q++) {
                int d;
                if (*q >= '0' && *q <= '9')
                    d = *q - '0';
                else if (*q >= 'a' && *q <= 'f')
                    d = *q - 'a' + 10;
                else if (*q >= 'A' && *q <= 'F')
                    d = *q - 'A' + 10;
                else
                    return '&';
                cp = cp * 16 + (uint32_t)d;
            }
        } else {
            for (const char *q = p + 1; q < semi; q++) {
                if (*q < '0' || *q > '9')
                    return '&';
                cp = cp * 10 + (uint32_t)(*q - '0');
            }
        }
        *consumed = (size_t)(semi - p) + 2; /* '&' + body + ';' */
        return cp;
    }
    size_t len = (size_t)(semi - p);
    *consumed = len + 2;
    return entity_lookup(p, len);
}

char *html_entity_decode(const char *s)
{
    if (!s)
        return NULL;
    strbuf sb = { 0 };
    for (const char *p = s; *p; p++) {
        if (*p == '&') {
            size_t consumed = 0;
            uint32_t cp = decode_entity(p + 1, &consumed);
            if (cp != '&' || consumed > 1) {
                sb_put_cp(&sb, cp);
                p += consumed - 1;
                continue;
            }
        }
        sb_put(&sb, *p);
    }
    if (!sb.buf)
        return xstrdup("");
    sanitize_inplace(sb.buf, false);
    return sb.buf;
}

static bool tag_is_break(const char *name, size_t len)
{
    static const char *brk[] = { "br", "p", "div", "li", "tr", "ul", "ol",
                                 "h1", "h2", "h3", "h4", "h5", "h6", "hr",
                                 "blockquote", "pre", "table", "section",
                                 "article", "header", "footer", "figure",
                                 NULL };
    for (int i = 0; brk[i]; i++) {
        if (strlen(brk[i]) == len && strncasecmp(brk[i], name, len) == 0)
            return true;
    }
    return false;
}

char *html_to_text(const char *html)
{
    if (!html)
        return xstrdup("");
    strbuf sb = { 0 };
    const char *p = html;
    while (*p) {
        if (*p == '<') {
            if (strncmp(p, "<!--", 4) == 0) {
                const char *end = strstr(p + 4, "-->");
                p = end ? end + 3 : p + strlen(p);
                continue;
            }
            const char *end = strchr(p, '>');
            if (!end) {
                p++;
                continue;
            }
            /* tag name */
            const char *name = p + 1;
            bool closing = false;
            if (*name == '/') {
                closing = true;
                name++;
            }
            const char *nend = name;
            while ((*nend >= 'a' && *nend <= 'z') ||
                   (*nend >= 'A' && *nend <= 'Z') ||
                   (*nend >= '0' && *nend <= '9'))
                nend++;
            size_t nlen = (size_t)(nend - name);

            if (!closing && nlen == 6 && strncasecmp(name, "script", 6) == 0) {
                const char *close = str_icontains(end, "</script");
                p = close ? close : end + 1;
                continue;
            }
            if (!closing && nlen == 5 && strncasecmp(name, "style", 5) == 0) {
                const char *close = str_icontains(end, "</style");
                p = close ? close : end + 1;
                continue;
            }
            if (tag_is_break(name, nlen))
                sb_put(&sb, '\n');
            p = end + 1;
            continue;
        }
        if (*p == '&') {
            size_t consumed = 0;
            uint32_t cp = decode_entity(p + 1, &consumed);
            if (consumed > 1) {
                sb_put_cp(&sb, cp);
                p += consumed;
                continue;
            }
        }
        sb_put(&sb, *p++);
    }
    if (!sb.buf)
        return xstrdup("");

    /* Final defence: drop any control/invisible code points or invalid UTF-8
     * that reached us as raw bytes, keeping intentional newlines. */
    sanitize_inplace(sb.buf, true);

    /* Normalise whitespace: trim trailing spaces per line, collapse runs of
     * blank lines, and drop leading/trailing blank lines. */
    char *out = xmalloc(sb.len + 1);
    size_t o = 0;
    int newlines = 0;
    for (char *q = sb.buf; *q;) {
        if (*q == '\n') {
            while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t'))
                o--;
            if (newlines < 2)
                out[o++] = '\n';
            newlines++;
            q++;
            continue;
        }
        if (*q == ' ' || *q == '\t') {
            if (o > 0 && out[o - 1] != ' ' && out[o - 1] != '\n')
                out[o++] = ' ';
            q++;
            continue;
        }
        out[o++] = *q++;
        newlines = 0;
    }
    while (o > 0 && (out[o - 1] == '\n' || out[o - 1] == ' '))
        o--;
    out[o] = '\0';
    free(sb.buf);
    return out;
}

/* ----------------------------------------------------------------------- */
/* paths                                                                   */
/* ----------------------------------------------------------------------- */

char *path_expand(const char *path)
{
    if (!path)
        return NULL;
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            if (path[1] == '\0')
                return xstrdup(home);
            return xasprintf("%s/%s", home, path + 2);
        }
    }
    return xstrdup(path);
}

char *path_join(const char *a, const char *b)
{
    if (!a || !*a)
        return xstrdup(b ? b : "");
    if (!b || !*b)
        return xstrdup(a);
    size_t alen = strlen(a);
    bool slash = a[alen - 1] == '/';
    return xasprintf("%s%s%s", a, slash ? "" : "/", b);
}

char *path_dirname(const char *path)
{
    if (!path)
        return xstrdup(".");
    const char *slash = strrchr(path, '/');
    if (!slash)
        return xstrdup(".");
    if (slash == path)
        return xstrdup("/");
    return xstrndup(path, (size_t)(slash - path));
}

int mkdir_p(const char *path, unsigned mode)
{
    if (!path || !*path)
        return -1;
    char *tmp = xstrdup(path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, (mode_t)mode) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    int rc = 0;
    if (mkdir(tmp, (mode_t)mode) != 0 && errno != EEXIST)
        rc = -1;
    free(tmp);
    return rc;
}

int ensure_private_dir(const char *path)
{
    if (!path || !*path)
        return -1;
    if (mkdir_p(path, 0700) != 0)
        return -1;

    struct stat st;
    if (lstat(path, &st) != 0) /* lstat: do not follow a symlink */
        return -1;
    if (!S_ISDIR(st.st_mode))
        return -1;
    if (st.st_uid != geteuid())
        return -1;
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        if (chmod(path, 0700) != 0)
            return -1;
    }
    return 0;
}

/* ----------------------------------------------------------------------- */
/* SHA-1 (via libcrypto, already a transitive dependency of libcurl)        */
/* ----------------------------------------------------------------------- */

void sha1_hex(const void *data, size_t len, char out[41])
{
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char *)data, len, digest);

    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0xf];
    }
    out[40] = '\0';
}

/* ----------------------------------------------------------------------- */
/* UTF-8                                                                   */
/* ----------------------------------------------------------------------- */

/* Decode one code point. Returns number of bytes consumed (>=1) and stores
 * the code point in *cp. Invalid sequences yield U+FFFD, consuming 1 byte. */
static size_t utf8_decode(const unsigned char *s, size_t remaining, uint32_t *cp)
{
    if (remaining == 0) {
        *cp = 0;
        return 0;
    }
    unsigned char c = s[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    size_t n;
    uint32_t v;
    if ((c & 0xE0) == 0xC0) {
        n = 2;
        v = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        v = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        v = c & 0x07;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    if (remaining < n) {
        *cp = 0xFFFD;
        return 1;
    }
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        v = (v << 6) | (s[i] & 0x3F);
    }
    *cp = v;
    return n;
}

size_t utf8_len(const char *s)
{
    if (!s)
        return 0;
    size_t count = 0;
    const unsigned char *p = (const unsigned char *)s;
    size_t remaining = strlen(s);
    while (remaining > 0) {
        uint32_t cp;
        size_t n = utf8_decode(p, remaining, &cp);
        if (n == 0)
            break;
        p += n;
        remaining -= n;
        count++;
    }
    return count;
}

size_t utf8_width(const char *s)
{
    if (!s)
        return 0;
    size_t width = 0;
    const unsigned char *p = (const unsigned char *)s;
    size_t remaining = strlen(s);
    while (remaining > 0) {
        uint32_t cp;
        size_t n = utf8_decode(p, remaining, &cp);
        if (n == 0)
            break;
        int w = wcwidth((wchar_t)cp);
        width += (w > 0) ? (size_t)w : (w == 0 ? 0u : 1u);
        p += n;
        remaining -= n;
    }
    return width;
}

size_t utf8_copy_cells(char *dst, size_t dstsz, const char *src, size_t max_cells)
{
    if (!dst || dstsz == 0)
        return 0;
    dst[0] = '\0';
    if (!src)
        return 0;
    size_t used = 0; /* bytes in dst */
    size_t cells = 0;
    const unsigned char *p = (const unsigned char *)src;
    size_t remaining = strlen(src);
    while (remaining > 0) {
        uint32_t cp;
        size_t n = utf8_decode(p, remaining, &cp);
        if (n == 0 || n > remaining)
            break;
        int w = wcwidth((wchar_t)cp);
        if (w < 0)
            w = 1; /* unknown/undecodable: assume single cell */
        if (cells + (size_t)w > max_cells)
            break;
        if (used + n + 1 > dstsz)
            break;
        memcpy(dst + used, p, n);
        used += n;
        cells += (size_t)w;
        p += n;
        remaining -= n;
    }
    dst[used] = '\0';
    return cells;
}

/* ----------------------------------------------------------------------- */
/* time                                                                    */
/* ----------------------------------------------------------------------- */

void format_age(time_t t, char *buf, size_t bufsz)
{
    if (!buf || bufsz == 0)
        return;
    if (t <= 0) {
        if (bufsz) buf[0] = '\0';
        return;
    }
    time_t now = time(NULL);
    long diff = (long)(now - t);
    if (diff < 0)
        diff = 0;
    if (diff < 60) {
        snprintf(buf, bufsz, "now");
    } else if (diff < 3600) {
        snprintf(buf, bufsz, "%ldm", diff / 60);
    } else if (diff < 86400) {
        snprintf(buf, bufsz, "%ldh", diff / 3600);
    } else if (diff < 7 * 86400) {
        snprintf(buf, bufsz, "%ldd", diff / 86400);
    } else {
        struct tm tm;
        localtime_r(&t, &tm);
        char tmp[32];
        if (tm.tm_year == localtime(&now)->tm_year)
            strftime(tmp, sizeof tmp, "%b %d", &tm);
        else
            strftime(tmp, sizeof tmp, "%b %d %Y", &tm);
        snprintf(buf, bufsz, "%s", tmp);
    }
}

void format_datetime(time_t t, char *buf, size_t bufsz)
{
    if (!buf || bufsz == 0)
        return;
    if (t <= 0) {
        if (bufsz) buf[0] = '\0';
        return;
    }
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, bufsz, "%Y-%m-%d %H:%M", &tm);
}
