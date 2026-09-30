#pragma once

// The tests: headless checks of what the client and the server share. Each suite is a
// function of checks; a check that fails says where and why, and the run ends with how
// many failed. They run from the project directory:
//
//   xmake test

#include <stdbool.h>

// --- checks ------------------------------------------------------------------------

void check_that(bool ok, const char *file, int line, const char *fmt, ...);

// A claim about the code; the message says what should be so.
#define CHECK(cond, ...) check_that((cond), __FILE__, __LINE__, __VA_ARGS__)

// --- the suites --------------------------------------------------------------------

void console_tests(void);
void color_tests(void);
