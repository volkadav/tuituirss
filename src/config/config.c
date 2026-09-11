// SPDX-License-Identifier: MIT
#include "config/config.h"
#include "util/util.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_default_path[4096];

const char *config_default_path(void)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        home = ".";
    snprintf(g_default_path, sizeof g_default_path, "%s/.tuituirssrc.json", home);
    return g_default_path;
}

static void set_str(char **dst, const char *val)
{
    free(*dst);
    *dst = val ? xstrdup(val) : NULL;
}

Config *config_new(void)
{
    Config *cfg = xcalloc(1, sizeof *cfg);
    cfg->timeout_sec = 30;
    cfg->insecure = false;
    cfg->debug = false;
    const char *home = getenv("HOME");
    cfg->data_dir = xasprintf("%s/.tuituirss", (home && *home) ? home : ".");
    return cfg;
}

void config_free(Config *cfg)
{
    if (!cfg)
        return;
    free(cfg->server_url);
    free(cfg->username);
    free(cfg->data_dir);
    free(cfg->ca_file);
    free(cfg->log_file);
    free(cfg);
}

static const char *cfg_json_str(json_t *obj, const char *key)
{
    json_t *v = json_object_get(obj, key);
    if (!v)
        return NULL;
    if (json_is_null(v))
        return NULL;
    if (!json_is_string(v))
        return NULL;
    return json_string_value(v);
}

int config_load_file(Config *cfg, const char *path)
{
    const char *use_path = path ? path : config_default_path();
    char *expanded = path_expand(use_path);

    json_error_t err;
    json_t *root = json_load_file(expanded, 0, &err);
    if (!root) {
        snprintf(cfg->error, sizeof cfg->error,
                 "cannot read config %s: %s", expanded, err.text);
        free(expanded);
        return -1;
    }
    if (!json_is_object(root)) {
        snprintf(cfg->error, sizeof cfg->error,
                 "config %s: top-level value must be an object", expanded);
        json_decref(root);
        free(expanded);
        return -1;
    }

    const char *s;
    if ((s = cfg_json_str(root, "server_url")))
        set_str(&cfg->server_url, s);
    if ((s = cfg_json_str(root, "username")))
        set_str(&cfg->username, s);
    if ((s = cfg_json_str(root, "data_dir")))
        set_str(&cfg->data_dir, s);
    if ((s = cfg_json_str(root, "ca_file")))
        set_str(&cfg->ca_file, s);
    if ((s = cfg_json_str(root, "log_file")))
        set_str(&cfg->log_file, s);

    json_t *v;
    if ((v = json_object_get(root, "insecure")) && json_is_boolean(v))
        cfg->insecure = json_is_true(v);
    if ((v = json_object_get(root, "debug")) && json_is_boolean(v))
        cfg->debug = json_is_true(v);
    if ((v = json_object_get(root, "timeout_sec")) && json_is_integer(v))
        cfg->timeout_sec = (int)json_integer_value(v);

    json_decref(root);
    free(expanded);
    return config_validate(cfg);
}

int config_validate(Config *cfg)
{
    if (!cfg->data_dir || !*cfg->data_dir)
        set_str(&cfg->data_dir, "~/.tuituirss");
    char *expanded = path_expand(cfg->data_dir);
    free(cfg->data_dir);
    cfg->data_dir = expanded;

    if (cfg->timeout_sec <= 0)
        cfg->timeout_sec = 30;

    if (!cfg->server_url || !*cfg->server_url) {
        snprintf(cfg->error, sizeof cfg->error, "server_url is not set");
        return -1;
    }
    if (!cfg->username || !*cfg->username) {
        snprintf(cfg->error, sizeof cfg->error, "username is not set");
        return -1;
    }
    return 0;
}
