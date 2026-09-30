#include "test.h"

#include <stdarg.h>
#include <stdio.h>

static int checks, failures;

void check_that(bool ok, const char *file, int line, const char *fmt, ...)
{
    checks++;
    if (ok) return;
    failures++;
    va_list args;
    va_start(args, fmt);
    printf("FAIL %s:%d: ", file, line);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

int main(void)
{
    console_tests();
    color_tests();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
