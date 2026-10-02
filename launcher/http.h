#pragma once

// HTTPS, through libcurl: GitHub's release pages and the files beside a release, which
// it reaches by a redirect or two. On Windows curl trusts what the system trusts
// (Schannel); on Linux it is built on mbedTLS, which knows no certificates of its own,
// so the distribution's bundle is found and given to it.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum HttpResult {
    HTTP_OK,
    HTTP_NOT_FOUND, // the server answered, and there is nothing there
    HTTP_FAILED,    // no answer, or a broken one; `error` says what
} HttpResult;

bool http_init(void);
void http_cleanup(void);

// The body of `url`, up to `max` bytes, NUL-terminated beyond `size`. Free it.
HttpResult http_get(const char *url, size_t max, char **body, size_t *size, char *error, size_t error_size);

// Bytes received so far, of `total` (0 until it is known).
typedef void (*HttpProgress)(void *user, uint64_t done, uint64_t total);

// `url` into the file `path`, hashed as it arrives: its SHA-256 and size come back.
HttpResult http_download(const char *url, const char *path, uint8_t sha256[32], uint64_t *size, HttpProgress progress,
                         void *user, char *error, size_t error_size);
