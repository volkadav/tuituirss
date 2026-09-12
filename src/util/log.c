// SPDX-License-Identifier: MIT
#include "util/util.h"

#include <fcntl.h>
#include <jansson.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static FILE *g_log = NULL;
static bool g_debug = false;

void log_init(const char *path, bool debug) {
  g_debug = debug;
  if (!path || !*path) {
    return;
  }
  int fd = open(path, O_CREAT | O_WRONLY | O_APPEND | O_NOFOLLOW, 0600);
  if (fd < 0) {
    return;
  }
  /* Make sure pre-existing files are not group/world readable. */
  fchmod(fd, 0600);
  g_log = fdopen(fd, "a");
  if (!g_log) {
    close(fd);
  }
}

void log_close(void) {
  if (g_log) {
    fclose(g_log);
    g_log = NULL;
  }
}

static void log_line(const char *level, const char *fmt, va_list ap) {
  if (!g_log) {
    return;
  }
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  char stamp[32];
  strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
  fprintf(g_log, "%s [%s] ", stamp, level);
  vfprintf(g_log, fmt, ap);
  fputc('\n', g_log);
  fflush(g_log);
}

void log_debug(const char *fmt, ...) {
  if (!g_log || !g_debug) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  log_line("debug", fmt, ap);
  va_end(ap);
}

void log_error(const char *fmt, ...) {
  if (!g_log) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  log_line("error", fmt, ap);
  va_end(ap);
}

/* Redact values of sensitive JSON keys (password, sid, session_id) so that a
 * request body can be logged without leaking credentials. Returns a newly
 * allocated string; caller frees. */
char *log_redact_json(const char *json) {
  if (!json) {
    return xstrdup("");
  }

  json_error_t err;
  json_t *obj = json_loads(json, 0, &err);
  if (!obj) {
    return xstrdup(json);
  }

  if (json_is_object(obj)) {
    static const char *keys[] = {"password", "sid", "session_id", NULL};
    for (int k = 0; keys[k]; k++) {
      if (json_object_get(obj, keys[k])) {
        json_object_set_new(obj, keys[k], json_string("***"));
      }
    }
  }

  char *out = json_dumps(obj, JSON_COMPACT);
  json_decref(obj);
  return out ? out : xstrdup("");
}
