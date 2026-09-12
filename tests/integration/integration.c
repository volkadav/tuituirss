// SPDX-License-Identifier: MIT
/*
 * Integration test: exercises the API layer against a live tt-rss instance.
 *
 * Required environment:
 *   TTRSS_URL   full API endpoint, e.g. https://host/tt-rss/api/
 *   TTRSS_USER  login
 *   TTRSS_PASS  password (or TTUIRSS_PASSWORD)
 *
 * Optional:
 *   TTRSS_INSECURE=1  skip TLS verification
 *
 * The test is intentionally non-destructive: it toggles the marked flag of a
 * headline twice so the server ends in the same state.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/api.h"
#include "config/config.h"
#include "util/util.h"

static int failures = 0;

#define REQUIRE(cond, msg)                                                     \
  do {                                                                         \
    if (cond) {                                                                \
      printf("  ok   %s\n", msg);                                              \
    } else {                                                                   \
      failures++;                                                              \
      printf("  FAIL %s (%s)\n", msg, api_last_error_string(api));             \
    }                                                                          \
  } while (0)

int main(void) {
  const char *url = getenv("TTRSS_URL");
  const char *user = getenv("TTRSS_USER");
  const char *pass = getenv("TTRSS_PASS");
  if (!pass) {
    pass = getenv("TTUIRSS_PASSWORD");
  }

  if (!url || !user || !pass) {
    fprintf(stderr, "integration: set TTRSS_URL, TTRSS_USER and TTRSS_PASS\n");
    return 2;
  }

  Config *cfg = config_new();
  free(cfg->server_url);
  cfg->server_url = xstrdup(url);
  free(cfg->username);
  cfg->username = xstrdup(user);
  cfg->insecure = getenv("TTRSS_INSECURE") != NULL;

  ApiClient *api = api_new(cfg);
  if (!api) {
    fprintf(stderr, "integration: cannot create API client\n");
    config_free(cfg);
    return 1;
  }

  printf("integration: %s as %s\n", url, user);

  REQUIRE(api_login(api, user, pass) == 0, "login");
  if (failures) {
    goto out;
  }

  char *version = NULL;
  REQUIRE(api_get_version(api, &version) == 0, "getVersion");
  printf("       server version: %s\n", version ? version : "?");
  free(version);

  Category *cats = NULL;
  size_t ncats = 0;
  REQUIRE(api_get_categories(api, false, true, &cats, &ncats) == 0,
          "getCategories");
  printf("       %zu categories\n", ncats);
  category_free(cats, ncats);

  Feed *feeds = NULL;
  size_t nfeeds = 0;
  REQUIRE(api_get_feeds(api, CATEGORY_ALL, false, false, &feeds, &nfeeds) == 0,
          "getFeeds");

  int feed_id = FEED_ALL;
  for (size_t i = 0; i < nfeeds; i++) {
    if (feeds[i].id > 0) {
      feed_id = feeds[i].id;
      break;
    }
  }
  feed_free(feeds, nfeeds);

  Headline *heads = NULL;
  size_t nheads = 0;
  REQUIRE(api_get_headlines(api, feed_id, false, 20, 0, VIEW_ALL_ARTICLES, NULL,
                            false, true, &heads, &nheads) == 0,
          "getHeadlines");
  printf("       %zu headlines from feed %d\n", nheads, feed_id);

  if (nheads > 0) {
    int id = heads[0].id;
    bool marked = heads[0].marked;

    Headline *art = NULL;
    size_t nart = 0;
    REQUIRE(api_get_article(api, id, &art, &nart) == 0, "getArticle");
    headline_free(art, nart);

    REQUIRE(api_update_article(api, &id, 1, UA_MODE_TOGGLE, UA_FIELD_MARKED,
                               NULL) == 0,
            "updateArticle toggle marked");
    REQUIRE(api_update_article(api, &id, 1,
                               marked ? UA_MODE_TRUE : UA_MODE_FALSE,
                               UA_FIELD_MARKED, NULL) == 0,
            "updateArticle restore marked");
  }
  headline_free(heads, nheads);

  Counter counters;
  REQUIRE(api_get_counters(api, &counters) == 0, "getCounters");
  printf("       unread=%d marked=%d\n", counters.unread, counters.marked);

  Label *labels = NULL;
  size_t nlabels = 0;
  REQUIRE(api_get_labels(api, 0, &labels, &nlabels) == 0, "getLabels");
  printf("       %zu labels\n", nlabels);
  label_free(labels, nlabels);

out:
  api_logout(api);
  api_free(api);
  config_free(cfg);
  printf("integration: %s\n", failures ? "FAILED" : "passed");
  return failures ? 1 : 0;
}
