// SPDX-License-Identifier: MIT
/*
 * libFuzzer harness for the untrusted-input parsers.
 *
 * Build/run:  make fuzz          (requires clang)
 *
 * Feeds arbitrary bytes to the pure functions that consume server/feed data:
 * string sanitization, HTML→text, UTF-8 helpers, the jansson model parsers and
 * the API envelope parser. Compiled with ASan+UBSan by the Makefile.
 */
#include <jansson.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "api/api.h"
#include "model/model.h"
#include "util/util.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > (1u << 20)) {
    return 0;
  } /* keep inputs bounded */

  char *buf = xmalloc(size + 1);
  memcpy(buf, data, size);
  buf[size] = '\0';

  /* string + HTML + UTF-8 helpers */
  char *s = xstrdup(buf);
  str_strip_ctrl(s);
  free(s);

  char *text = html_to_text(buf);
  free(text);
  char *dec = html_entity_decode(buf);
  free(dec);

  char cells[64];
  utf8_copy_cells(cells, sizeof cells, buf, 40);
  (void)utf8_width(buf);
  (void)utf8_len(buf);

  /* model parsers on whatever JSON the input happens to be */
  json_error_t err;
  json_t *j = json_loads(buf, 0, &err);
  if (j) {
    size_t n = 0;
    if (json_is_array(j)) {
      Category *c = parse_categories(j, &n);
      category_free(c, n);
      Feed *f = parse_feeds(j, &n);
      feed_free(f, n);
      Headline *h = parse_headlines(j, &n);
      headline_free(h, n);
      Label *l = parse_labels(j, &n);
      label_free(l, n);
    } else if (json_is_object(j)) {
      Counter c = parse_counters(j);
      (void)c;
    }
    json_decref(j);
  }

  /* response envelope parser */
  ApiError ae;
  char msg[128];
  json_t *content = NULL;
  if (api_parse_envelope(buf, &ae, msg, sizeof msg, &content) == 0) {
    json_decref(content);
  }

  free(buf);
  return 0;
}
