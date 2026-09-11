// SPDX-License-Identifier: MIT
#include "test.h"
#include "api/session.h"
#include "config/config.h"
#include "util/util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static Config *make_cfg(const char *dir)
{
    Config *cfg = config_new();
    free(cfg->data_dir);
    cfg->data_dir = xstrdup(dir);
    free(cfg->server_url);
    cfg->server_url = xstrdup("https://example.test/tt-rss/api/");
    free(cfg->username);
    cfg->username = xstrdup("tester");
    return cfg;
}

void test_session(void)
{
    char tmpl[] = "/tmp/tuituirss-sess-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    if (!dir)
        return;

    Config *cfg = make_cfg(dir);

    /* save / load round trip, mode 0600 */
    CHECK(session_save(cfg, "SID123", 23) == 0);
    autofree char *path = session_path(cfg);
    struct stat st;
    CHECK(stat(path, &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);

    char *sid = NULL;
    int level = 0;
    CHECK(session_load(cfg, &sid, &level) == 0);
    CHECK_STR(sid, "SID123");
    CHECK(level == 23);
    free(sid);

    /* clear removes it */
    CHECK(session_clear(cfg) == 0);
    CHECK(access(path, F_OK) != 0);

    /* symlink attack: a planted symlink must not be written through */
    autofree char *victim = path_join(dir, "victim.txt");
    int vfd = open(victim, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    CHECK(vfd >= 0);
    if (vfd >= 0) {
        CHECK(write(vfd, "SECRET", 6) == 6);
        close(vfd);
    }
    CHECK(symlink(victim, path) == 0);
    CHECK(session_save(cfg, "EVIL", 23) == -1);

    char vbuf[16] = { 0 };
    vfd = open(victim, O_RDONLY);
    if (vfd >= 0) {
        ssize_t n = read(vfd, vbuf, sizeof vbuf - 1);
        close(vfd);
        if (n > 0)
            vbuf[n] = '\0';
    }
    CHECK_STR(vbuf, "SECRET");
    unlink(path);

    /* a symlinked data_dir is rejected */
    autofree char *linkdir = path_join(dir, "linkdir");
    CHECK(symlink(dir, linkdir) == 0);
    CHECK(ensure_private_dir(linkdir) == -1);
    unlink(linkdir);

    /* existing overly-permissive directory is tightened to 0700 */
    CHECK(chmod(dir, 0755) == 0);
    CHECK(ensure_private_dir(dir) == 0);
    CHECK(stat(dir, &st) == 0);
    CHECK((st.st_mode & 0777) == 0700);

    config_free(cfg);
    unlink(victim);
    rmdir(dir);
}
