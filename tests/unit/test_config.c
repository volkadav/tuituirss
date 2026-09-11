// SPDX-License-Identifier: MIT
#include "config/config.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *write_temp(const char *contents) {
  char *path = strdup("/tmp/tuituirss-test-XXXXXX.json");
  int fd = mkstemps(path, 5);
  if (fd < 0) {
    free(path);
    return NULL;
  }
  size_t len = strlen(contents);
  ssize_t n = write(fd, contents, len);
  (void)n;
  close(fd);
  return path;
}

void test_config(void) {
  char *path = write_temp("{\n"
                          "  \"server_url\": \"https://example.test/api/\",\n"
                          "  \"username\": \"alice\",\n"
                          "  \"insecure\": true,\n"
                          "  \"timeout_sec\": 12,\n"
                          "  \"data_dir\": \"/tmp/tuituirss-data\"\n"
                          "}\n");
  CHECK(path != NULL);
  Config *cfg = config_new();
  CHECK(config_load_file(cfg, path) == 0);
  CHECK_STR(cfg->server_url, "https://example.test/api/");
  CHECK_STR(cfg->username, "alice");
  CHECK(cfg->insecure == true);
  CHECK(cfg->timeout_sec == 12);
  CHECK_STR(cfg->data_dir, "/tmp/tuituirss-data");
  config_free(cfg);
  unlink(path);
  free(path);

  /* missing required fields should fail validation */
  path = write_temp("{\n  \"username\": \"bob\"\n}\n");
  cfg = config_new();
  CHECK(config_load_file(cfg, path) == -1);
  config_free(cfg);
  unlink(path);
  free(path);

  /* malformed JSON should fail */
  path = write_temp("{ not json");
  cfg = config_new();
  CHECK(config_load_file(cfg, path) == -1);
  config_free(cfg);
  unlink(path);
  free(path);

  /* defaults */
  cfg = config_new();
  CHECK(cfg->timeout_sec == 30);
  CHECK(cfg->data_dir != NULL && strstr(cfg->data_dir, ".tuituirss") != NULL);
  config_free(cfg);
}
