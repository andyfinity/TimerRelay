// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
// storage - thin NVS wrapper. NVS provides built-in wear leveling across its
// (deliberately oversized) partition; callers add write-on-change on top.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise NVS (handles the "needs erase" case on first boot / version bump).
void storage_init(void);

// Read a blob into `out`. Returns true only if a stored value of exactly
// `len` bytes was found.
bool storage_get_blob(const char *key, void *out, size_t len);

// Write a blob and commit. NVS itself skips the physical write if the value is
// unchanged, but callers should still gate on change to avoid the commit churn.
esp_err_t storage_set_blob(const char *key, const void *in, size_t len);

#ifdef __cplusplus
}
#endif
