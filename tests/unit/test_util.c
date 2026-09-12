// SPDX-License-Identifier: MIT
#include "test.h"
#include "ui/ui.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>

void test_util(void) {
  char hex[41];

  sha1_hex("abc", 3, hex);
  CHECK_STR(hex, "a9993e364706816aba3e25717850c26c9cd0d89d");

  sha1_hex("", 0, hex);
  CHECK_STR(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709");

  char *joined = path_join("/a/b", "c");
  CHECK_STR(joined, "/a/b/c");
  free(joined);
  joined = path_join("/a/b/", "c");
  CHECK_STR(joined, "/a/b/c");
  free(joined);
  joined = path_join("/a/b", NULL);
  CHECK_STR(joined, "/a/b");
  free(joined);

  char *dir = path_dirname("/a/b/c.json");
  CHECK_STR(dir, "/a/b");
  free(dir);
  dir = path_dirname("plain");
  CHECK_STR(dir, ".");
  free(dir);

  char buf[64];
  strcpy(buf, "  hi there \t");
  CHECK_STR(str_trim(buf), "hi there");

  strcpy(buf, "a\r\nb\tc");
  str_strip_ctrl(buf);
  CHECK_STR(buf, "a  b\tc");

  CHECK(utf8_len("hello") == 5);
  CHECK(utf8_len("h\xc3\xa9llo") == 5); /* héllo */
  CHECK(utf8_width("ab") == 2);

  char cells[8];
  size_t w = utf8_copy_cells(cells, sizeof cells, "abcdef", 3);
  CHECK(w == 3);
  CHECK_STR(cells, "abc");

  /* never split a multibyte code point */
  w = utf8_copy_cells(cells, sizeof cells, "\xc3\xa9\xc3\xa9", 1);
  CHECK(w == 1);
  CHECK_STR(cells, "\xc3\xa9");

  char *red = log_redact_json("{\"user\":\"bob\",\"password\":\"hunter2\"}");
  CHECK(red && strstr(red, "hunter2") == NULL);
  CHECK(red && strstr(red, "\"bob\"") != NULL);
  free(red);

  char *txt = html_to_text("<p>Hello <b>world</b></p><script>x()</script>"
                           "<p>a &amp; b &#233;</p>");
  CHECK(txt && strstr(txt, "<") == NULL);
  CHECK(txt && strstr(txt, "Hello world") != NULL);
  CHECK(txt && strstr(txt, "a & b") != NULL);
  CHECK(txt && strstr(txt, "x()") == NULL);
  CHECK(txt && strstr(txt, "\xc3\xa9") != NULL); /* é */
  free(txt);

  char *dec = html_entity_decode("caf&#233; &lt;x&gt;");
  CHECK_STR(dec, "caf\xc3\xa9 <x>");
  free(dec);

  /* --- URL extraction ------------------------------------------------ */
  size_t nurls = 0;
  char **urls =
      url_extract("see https://example.com/a and http://b.test/x.", &nurls);
  CHECK(nurls == 2);
  CHECK_STR(urls[0], "https://example.com/a");
  CHECK_STR(urls[1], "http://b.test/x");
  url_free(urls, nurls);

  /* trailing punctuation and unmatched parens are trimmed */
  urls = url_extract("(see http://x.test/y) done", &nurls);
  CHECK(nurls == 1);
  CHECK_STR(urls[0], "http://x.test/y");
  url_free(urls, nurls);

  /* matched parens inside a URL are preserved */
  urls = url_extract("(https://en.wikipedia.org/wiki/C_(language)) ok", &nurls);
  CHECK(nurls == 1);
  CHECK_STR(urls[0], "https://en.wikipedia.org/wiki/C_(language)");
  url_free(urls, nurls);

  urls = url_extract("no links here", &nurls);
  CHECK(nurls == 0);
  CHECK(urls == NULL);
  url_free(urls, nurls);

  /* --- color theme names -------------------------------------------- */
  CHECK(render_theme_valid("dark"));
  CHECK(render_theme_valid("light"));
  CHECK(render_theme_valid("mono"));
  CHECK(!render_theme_valid("solarized"));
  CHECK(!render_theme_valid(""));
  CHECK(!render_theme_valid(NULL));

  /* --- adversarial sanitization ------------------------------------- */

  /* raw ESC (ANSI) is stripped */
  char ctrl[64];
  strcpy(ctrl, "a\x1b[31mred");
  str_strip_ctrl(ctrl);
  CHECK(strchr(ctrl, 0x1b) == NULL);
  CHECK_STR(ctrl, "a[31mred");

  /* DEL is stripped */
  strcpy(ctrl, "x\x7f"
               "y");
  str_strip_ctrl(ctrl);
  CHECK_STR(ctrl, "xy");

  /* C1 CSI (U+009B, encoded C2 9B) is stripped */
  strcpy(ctrl, "a\xc2\x9b"
               "b");
  str_strip_ctrl(ctrl);
  CHECK_STR(ctrl, "ab");

  /* bidi override (U+202E, encoded E2 80 AE) is stripped */
  strcpy(ctrl, "a\xe2\x80\xae"
               "b");
  str_strip_ctrl(ctrl);
  CHECK_STR(ctrl, "ab");

  /* invalid UTF-8 bytes are dropped, valid text preserved */
  strcpy(ctrl, "a\xff"
               "b");
  str_strip_ctrl(ctrl);
  CHECK_STR(ctrl, "ab");

  /* entity-encoded control characters cannot smuggle ESC/NUL/bidi */
  char *e1 = html_entity_decode("&#x1b;[31mred");
  CHECK(e1 && strchr(e1, 0x1b) == NULL);
  free(e1);

  char *e2 = html_entity_decode("a&#0;b");
  CHECK(e2 && strlen(e2) == 5); /* 'a' + U+FFFD (3 bytes) + 'b' */
  CHECK(e2 && strstr(e2, "\xef\xbf\xbd") != NULL);
  free(e2);

  char *e3 = html_entity_decode("a&#x202e;b");
  CHECK(e3 && strstr(e3, "\xe2\x80\xae") == NULL);
  free(e3);

  char *e4 = html_entity_decode("&#xD800;"); /* lone surrogate */
  CHECK(e4 && strstr(e4, "\xef\xbf\xbd") != NULL);
  free(e4);

  /* html_to_text neutralises entity-encoded escapes but keeps newlines */
  char *e5 = html_to_text("<p>&#27;[31mred</p><p>second</p>");
  CHECK(e5 && strchr(e5, 0x1b) == NULL);
  CHECK(e5 && strstr(e5, "red") != NULL);
  CHECK(e5 && strchr(e5, '\n') != NULL); /* block elements stay on lines */
  free(e5);
}
