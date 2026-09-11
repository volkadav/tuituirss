// SPDX-License-Identifier: MIT
#include "model/model.h"
#include "test.h"

#include <jansson.h>
#include <stdlib.h>

void test_model(void) {
  /* categories */
  json_t *j = json_loads("[{\"id\":3,\"title\":\"News\",\"unread\":7},"
                         " {\"id\":-2,\"title\":\"Labels\",\"unread\":0}]",
                         0, NULL);
  size_t n = 0;
  Category *cats = parse_categories(j, &n);
  CHECK(n == 2);
  CHECK(cats[0].id == 3);
  CHECK_STR(cats[0].title, "News");
  CHECK(cats[0].unread == 7);
  CHECK(cats[1].id == CATEGORY_LABELS);
  category_free(cats, n);
  json_decref(j);

  /* feeds */
  j = json_loads(
      "[{\"id\":11,\"title\":\"blog\",\"feed_url\":\"https://x/feed\","
      "\"cat_id\":3,\"unread\":2,\"has_icon\":true,"
      "\"last_updated\":1700000000,\"is_cat\":false}]",
      0, NULL);
  Feed *feeds = parse_feeds(j, &n);
  CHECK(n == 1);
  CHECK(feeds[0].id == 11);
  CHECK_STR(feeds[0].title, "blog");
  CHECK(feeds[0].cat_id == 3);
  CHECK(feeds[0].unread == 2);
  CHECK(feeds[0].has_icon == true);
  CHECK(feeds[0].last_updated == 1700000000);
  feed_free(feeds, n);
  json_decref(j);

  /* headlines, including string/bool coercion and nested labels */
  j = json_loads(
      "[{\"id\":100,\"title\":\"Hello\",\"unread\":true,\"marked\":false,"
      "\"published\":false,\"updated\":1700000123,\"feed_id\":11,"
      "\"feed_title\":\"blog\",\"link\":\"https://x/1\",\"score\":5,"
      "\"labels\":[[-1024,\"work\",\"#fff\",\"#000\"]],"
      "\"attachments\":[{\"url\":\"https://x/a.mp3\","
      "\"content_type\":\"audio/mpeg\"}]}]",
      0, NULL);
  Headline *h = parse_headlines(j, &n);
  CHECK(n == 1);
  CHECK(h[0].id == 100);
  CHECK_STR(h[0].title, "Hello");
  CHECK(h[0].unread == true);
  CHECK(h[0].marked == false);
  CHECK(h[0].score == 5);
  CHECK(h[0].updated == 1700000123);
  CHECK(h[0].nlabels == 1);
  CHECK_STR(h[0].labels[0].caption, "work");
  CHECK(h[0].nattachments == 1);
  CHECK_STR(h[0].attachments[0].url, "https://x/a.mp3");
  headline_free(h, n);
  json_decref(j);

  /* labels (object form) */
  j = json_loads("[{\"id\":-1024,\"caption\":\"work\",\"fg_color\":\"#fff\","
                 "\"bg_color\":\"#000\",\"checked\":true}]",
                 0, NULL);
  Label *labels = parse_labels(j, &n);
  CHECK(n == 1);
  CHECK(labels[0].id == LABEL_BASE_INDEX);
  CHECK_STR(labels[0].caption, "work");
  CHECK(labels[0].checked == true);
  label_free(labels, n);
  json_decref(j);

  /* counters, flat */
  j = json_loads("{\"unread\":42,\"marked\":3,\"published\":1}", 0, NULL);
  Counter c = parse_counters(j);
  CHECK(c.unread == 42);
  CHECK(c.marked == 3);
  CHECK(c.published == 1);
  json_decref(j);

  /* classification */
  CHECK(model_classify_feed_id(11) == ITEM_FEED);
  CHECK(model_classify_feed_id(FEED_STARRED) == ITEM_VIRTUAL);
  CHECK(model_classify_feed_id(LABEL_BASE_INDEX - 2) == ITEM_LABEL);
  CHECK(model_classify_feed_id(PLUGIN_FEED_BASE_INDEX) == ITEM_PLUGIN);
  CHECK(model_is_virtual_feed(FEED_ARCHIVED));
  CHECK_STR(model_virtual_feed_title(FEED_STARRED), "Starred");

  /* NULL / empty inputs are safe */
  CHECK(parse_categories(NULL, &n) == NULL && n == 0);
}
