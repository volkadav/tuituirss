// SPDX-License-Identifier: MIT
#ifndef TUIIRSS_TEST_H
#define TUIIRSS_TEST_H

#include <stdio.h>
#include <string.h>

extern int g_checks;
extern int g_fails;

#define CHECK(cond)                                                            \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      g_fails++;                                                               \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                          \
  } while (0)

void check_str_impl(const char *a, const char *b, const char *file, int line);

#define CHECK_STR(a, b) check_str_impl((a), (b), __FILE__, __LINE__)

void test_util(void);
void test_config(void);
void test_model(void);
void test_api(void);
void test_session(void);

#endif /* TUIIRSS_TEST_H */
