// SPDX-License-Identifier: MIT
#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "api/api.h"
#include "api/session.h"
#include "config/config.h"
#include "ui/ui.h"
#include "util/util.h"

#define TUIIRSS_VERSION "0.0.2"

static void usage(FILE *out) {
  fprintf(out,
          "tuituirss %s — a terminal client for Tiny Tiny RSS\n"
          "\n"
          "Usage: tuituirss [options]\n"
          "\n"
          "Options:\n"
          "  -c, --config PATH   config file (default ~/.tuituirssrc.json)\n"
          "      --theme NAME    color theme: dark (default), light or mono\n"
          "      --dark          shorthand for --theme dark\n"
          "      --light         shorthand for --theme light\n"
          "      --mono          shorthand for --theme mono (no colors)\n"
          "  -h, --help          show this help\n"
          "  -V, --version       show version\n"
          "\n"
          "The password is read from $TTUIRSS_PASSWORD when set, otherwise\n"
          "prompted for on startup. It is never written to disk.\n",
          TUIIRSS_VERSION);
}

static char *prompt_password(const char *prompt) {
  FILE *tty = fopen("/dev/tty", "r+");
  FILE *in = tty ? tty : stdin;
  FILE *out = tty ? tty : stderr;

  struct termios old, mod;
  bool have = tcgetattr(fileno(in), &old) == 0;
  if (have) {
    mod = old;
    mod.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(fileno(in), TCSAFLUSH, &mod);
  }
  fputs(prompt, out);
  fflush(out);

  char buf[512] = "";
  if (!fgets(buf, sizeof buf, in)) {
    buf[0] = '\0';
  }

  if (have) {
    tcsetattr(fileno(in), TCSAFLUSH, &old);
  }
  fputs("\n", out);
  if (tty) {
    fclose(tty);
  }

  buf[strcspn(buf, "\r\n")] = '\0';
  return xstrdup(buf);
}

int main(int argc, char **argv) {
  const char *config_path = NULL;
  const char *theme_override = NULL;

  for (int i = 1; i < argc; i++) {
    if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) &&
        i + 1 < argc) {
      config_path = argv[++i];
    } else if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc) {
      theme_override = argv[++i];
    } else if (strcmp(argv[i], "--dark") == 0) {
      theme_override = "dark";
    } else if (strcmp(argv[i], "--light") == 0) {
      theme_override = "light";
    } else if (strcmp(argv[i], "--mono") == 0) {
      theme_override = "mono";
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(stdout);
      return 0;
    } else if (strcmp(argv[i], "-V") == 0 ||
               strcmp(argv[i], "--version") == 0) {
      printf("tuituirss %s\n", TUIIRSS_VERSION);
      return 0;
    } else {
      fprintf(stderr, "tuituirss: unknown option '%s'\n", argv[i]);
      usage(stderr);
      return 2;
    }
  }

  Config *cfg = config_new();
  if (config_load_file(cfg, config_path) != 0) {
    fprintf(stderr, "tuituirss: %s\n", cfg->error);
    if (!config_path) {
      fprintf(stderr, "Create %s (see tuituirssrc.example.json).\n",
              config_default_path());
    }
    config_free(cfg);
    return 1;
  }

  if (theme_override) {
    free(cfg->theme);
    cfg->theme = xstrdup(theme_override);
  }

  if (ensure_private_dir(cfg->data_dir) != 0) {
    fprintf(stderr,
            "tuituirss: warning: cannot secure data_dir %s "
            "(session cache and logs may be disabled)\n",
            cfg->data_dir);
  }

  autofree char *log_path = NULL;
  if (cfg->debug) {
    log_path = cfg->log_file ? path_expand(cfg->log_file)
                             : path_join(cfg->data_dir, "tuituirss.log");
  }
  log_init(log_path, cfg->debug);

  curl_global_init(CURL_GLOBAL_DEFAULT);
  ApiClient *api = api_new(cfg);
  if (!api) {
    fprintf(stderr, "tuituirss: failed to initialize HTTP client\n");
    config_free(cfg);
    curl_global_cleanup();
    return 1;
  }

  /* Reuse a cached session when possible. */
  autofree char *sid = NULL;
  int level = 0;
  if (session_load(cfg, &sid, &level) == 0) {
    api_set_session(api, sid, level);
    log_debug("loaded cached session");
  }

  bool logged_in = false;
  if (api_is_logged_in(api, &logged_in) != 0 || !logged_in) {
    const char *pw = getenv("TTUIRSS_PASSWORD");
    autofree char *pwbuf = NULL;
    if (!pw || !*pw) {
      char prompt[256];
      snprintf(prompt, sizeof prompt, "Password for %s@%s: ", cfg->username,
               cfg->server_url);
      pwbuf = prompt_password(prompt);
      pw = pwbuf;
    }
    if (!pw || !*pw) {
      fprintf(stderr, "tuituirss: no password supplied\n");
      api_free(api);
      config_free(cfg);
      curl_global_cleanup();
      return 1;
    }
    api_set_credentials(api, cfg->username, pw);
    if (api_login(api, cfg->username, pw) != 0) {
      fprintf(stderr, "tuituirss: login failed: %s\n",
              api_last_error_string(api));
      if (api_last_error(api) == API_ERR_API_DISABLED) {
        fprintf(stderr,
                "Enable \"Enable API access\" in tt-rss preferences.\n");
      }
      api_free(api);
      config_free(cfg);
      curl_global_cleanup();
      return 1;
    }
    session_save(cfg, api_session_id(api), api_level(api));
    log_debug("logged in, api level %d", api_level(api));
  }

  api_get_version(api, NULL);
  log_debug("server version %s, api level %d",
            api_server_version(api) ? api_server_version(api) : "?",
            api_level(api));

  App *app = app_new(cfg, api);
  ui_run(app);
  app_free(app);

  api_free(api);
  config_free(cfg);
  log_close();
  curl_global_cleanup();
  return 0;
}
