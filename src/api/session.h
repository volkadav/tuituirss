// SPDX-License-Identifier: MIT
#ifndef TUIIRSS_SESSION_H
#define TUIIRSS_SESSION_H

#include "config/config.h"

/* Session cache, keyed by a hash of server URL + username. The password is
 * never stored. Files are created mode 0600 under cfg->data_dir. */

/* Return the cache file path (newly allocated). */
char *session_path(const Config *cfg);

/* Load a cached session. Returns 0 on success, -1 if none/unreadable. */
int session_load(const Config *cfg, char **sid_out, int *level_out);

/* Persist a session. Returns 0 on success. */
int session_save(const Config *cfg, const char *sid, int level);

/* Remove the cached session file. Returns 0 on success or if absent. */
int session_clear(const Config *cfg);

#endif /* TUIIRSS_SESSION_H */
