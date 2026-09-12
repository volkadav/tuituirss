// SPDX-License-Identifier: MIT
#ifndef TUIIRSS_CONFIG_H
#define TUIIRSS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

/* Runtime configuration, loaded from JSON and optionally overridden on the
 * command line. All strings are owned by the struct. */
typedef struct {
  char *server_url; /* full API endpoint URL */
  char *username;
  char *data_dir;  /* expanded absolute-ish path */
  char *ca_file;   /* optional CA bundle */
  char *browser;   /* command used to open links (e.g. "xdg-open") */
  char *theme;     /* color theme: "dark" (default) or "light" */
  bool insecure;   /* skip TLS verification (self-signed certs) */
  int timeout_sec; /* network timeout, seconds */
  bool debug;      /* enable debug logging */
  char *log_file;  /* optional log path; NULL disables logging */

  char error[256]; /* last error message */
} Config;

/* Allocate a Config with defaults filled in. */
Config *config_new(void);
void config_free(Config *cfg);

/* Default config path (~/.tuituirssrc.json). Returned string is static. */
const char *config_default_path(void);

/* Load JSON config from path (NULL means the default path). Fields present
 * override the defaults. Returns 0 on success, -1 on error (cfg->error set). */
int config_load_file(Config *cfg, const char *path);

/* Fill any unset fields with defaults and validate. Returns 0 or -1. */
int config_validate(Config *cfg);

#endif /* TUIIRSS_CONFIG_H */
