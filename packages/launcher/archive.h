#pragma once

// The packages' archives, unpacked: a zip (Windows) or a tar.gz (Linux), told apart by
// their first bytes. Each package's entries sit under one directory named after it
// (soldatreloaded-0.5.0-windows-x64/...); that directory is dropped, so what
// lands in `into` is laid out as an install is. An entry whose path would reach outside
// `into` stops the unpacking.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Bytes written so far, of `total` (0 if it isn't known).
typedef void (*ArchiveProgress)(void *user, uint64_t done, uint64_t total);

bool archive_extract(const char *archive, const char *into, ArchiveProgress progress, void *user, char *error,
                     size_t error_size);
