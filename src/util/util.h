// SPDX-License-Identifier: MIT
#ifndef TUIIRSS_UTIL_H
#define TUIIRSS_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

/* ---- scope-based cleanup ---------------------------------------------- */
/*
 * `autofree` uses the GCC/Clang `cleanup` attribute to free a heap pointer
 * automatically when it goes out of scope:
 *
 *     autofree char *s = xstrdup(...);   // freed at scope exit
 *
 * Always initialise the variable (use NULL if the value is assigned later);
 * the handler calls free() unconditionally. Do not use it on a pointer whose
 * ownership is transferred (returned, or stored into another owner).
 */
#if defined(__GNUC__) || defined(__clang__)
static inline void autofree_cleanup(void *p)
{
    free(*(void **)p);
}
#define autofree __attribute__((cleanup(autofree_cleanup)))
#else
#define autofree
#endif

/* ---- allocation (abort on OOM) ---------------------------------------- */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
/* printf into a freshly allocated string (caller frees). */
char *xasprintf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2))) __attribute__((nonnull(1)));

/* ---- strings ---------------------------------------------------------- */
/* Remove control characters (including \r and NUL) in place. */
void str_strip_ctrl(char *s);
/* Trim leading/trailing ASCII whitespace in place; returns s. */
char *str_trim(char *s);
/* Case-insensitive substring search; NULL if not found. */
const char *str_icontains(const char *haystack, const char *needle);
/* Stable, display-safe copy of an untrusted string (control chars removed). */
char *str_sanitize_copy(const char *s);

/* Decode HTML entities (&amp;, &#39;, &#x2014;, ...) into a new string. */
char *html_entity_decode(const char *s);
/* Convert untrusted HTML into plain text: tags removed, block elements turned
 * into newlines, entities decoded. The result is safe to draw in a terminal. */
char *html_to_text(const char *html);

/* ---- paths ------------------------------------------------------------ */
/* Expand a leading "~/" to $HOME. Returns a newly allocated string. */
char *path_expand(const char *path);
/* Join two path components with '/'. Returns newly allocated string. */
char *path_join(const char *a, const char *b);
/* mkdir -p. Returns 0 on success. */
int mkdir_p(const char *path, unsigned mode);
/* Create (if needed) and secure a private directory: must be a real directory
 * (not a symlink), owned by the current euid, with no group/other access.
 * Existing overly-permissive permissions are tightened to 0700. */
int ensure_private_dir(const char *path);
/* Return directory portion of a path (newly allocated). */
char *path_dirname(const char *path);

/* ---- hashing ---------------------------------------------------------- */
/* Lowercase hex SHA-1 of the given buffer. out must hold 41 bytes. */
void sha1_hex(const void *data, size_t len, char out[41]);

/* ---- utf-8 ------------------------------------------------------------ */
/* Number of Unicode code points in a NUL-terminated UTF-8 string. */
size_t utf8_len(const char *s);
/* Copy at most max_cells display columns of src into dst (NUL terminated),
 * never splitting a code point. Returns display width written. */
size_t utf8_copy_cells(char *dst, size_t dstsz, const char *src, size_t max_cells);
/* Display width of a UTF-8 string (approximate; wide chars count 2). */
size_t utf8_width(const char *s);

/* ---- time ------------------------------------------------------------- */
/* Human friendly age, e.g. "2h", "3d", "Jan 5". Returns static-ish buffer
 * is avoided; caller supplies buffer. */
void format_age(time_t t, char *buf, size_t bufsz);
/* "YYYY-MM-DD HH:MM". */
void format_datetime(time_t t, char *buf, size_t bufsz);

/* ---- logging (implemented in log.c) ---------------------------------- */
void log_init(const char *path, bool debug);
void log_close(void);
void log_debug(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void log_error(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
/* Return a copy of a JSON string with sensitive values redacted. */
char *log_redact_json(const char *json);

#endif /* TUIIRSS_UTIL_H */
