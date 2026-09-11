// SPDX-License-Identifier: MIT
#include "test.h"

#include <locale.h>

int g_checks = 0;
int g_fails = 0;

void check_str_impl(const char *a, const char *b, const char *file, int line)
{
    g_checks++;
    if (!a || !b || strcmp(a, b) != 0) {
        g_fails++;
        fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", file, line,
                a ? a : "(null)", b ? b : "(null)");
    }
}

int main(void)
{
    setlocale(LC_ALL, "");
    printf("tuituirss unit tests\n");
    test_util();
    test_config();
    test_model();
    test_api();
    test_session();
    printf("\n%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
