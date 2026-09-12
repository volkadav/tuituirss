// SPDX-License-Identifier: MIT
#include "api/session.h"
#include "util/util.h"

#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *session_path(const Config *cfg) {
  autofree char *key =
      xasprintf("%s\n%s", cfg->server_url ? cfg->server_url : "",
                cfg->username ? cfg->username : "");
  char hash[41];
  sha1_hex(key, strlen(key), hash);
  autofree char *name = xasprintf("session-%s.json", hash);
  return path_join(cfg->data_dir, name);
}

int session_load(const Config *cfg, char **sid_out, int *level_out) {
  if (sid_out) {
    *sid_out = NULL;
  }
  if (level_out) {
    *level_out = 0;
  }

  autofree char *path = session_path(cfg);
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) {
    return -1;
  }
  json_error_t err;
  json_t *root = json_loadfd(fd, 0, &err);
  close(fd);
  if (!root) {
    return -1;
  }

  int rc = -1;
  const char *sid = json_string_value(json_object_get(root, "session_id"));
  if (sid && *sid) {
    if (sid_out) {
      *sid_out = xstrdup(sid);
    }
    if (level_out) {
      *level_out = (int)json_integer_value(json_object_get(root, "api_level"));
    }
    rc = 0;
  }
  json_decref(root);
  return rc;
}

int session_save(const Config *cfg, const char *sid, int level) {
  if (!sid || !*sid) {
    return -1;
  }
  if (ensure_private_dir(cfg->data_dir) != 0) {
    return -1;
  }

  json_t *root = json_object();
  json_object_set_new(root, "session_id", json_string(sid));
  json_object_set_new(root, "api_level", json_integer(level));
  json_object_set_new(root, "server_url",
                      json_string(cfg->server_url ? cfg->server_url : ""));

  autofree char *path = session_path(cfg);
  /* O_NOFOLLOW: never write through a planted symlink. The file is created
   * with 0600 and fchmod'd in case it already existed with looser modes. */
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  if (fd < 0) {
    json_decref(root);
    return -1;
  }
  fchmod(fd, 0600);
  FILE *fp = fdopen(fd, "w");
  if (!fp) {
    close(fd);
    json_decref(root);
    return -1;
  }
  int rc = json_dumpf(root, fp, JSON_INDENT(2) | JSON_COMPACT);
  if (fflush(fp) != 0) {
    rc = -1;
  }
  fclose(fp);
  json_decref(root);
  return rc == 0 ? 0 : -1;
}

int session_clear(const Config *cfg) {
  autofree char *path = session_path(cfg);
  int rc = unlink(path);
  return (rc == 0 || errno == ENOENT) ? 0 : -1;
}
