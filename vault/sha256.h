/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- SHA-256 (compact, self-contained)
 * Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only
 */

#ifndef NX_SHA256_H
#define NX_SHA256_H

#include <stddef.h>
#include <stdint.h>

void nx_sha256(const void *data, size_t len, uint8_t out[32]);
void nx_hmac_sha256(const void *key, size_t key_len,
                    const void *data, size_t data_len,
                    uint8_t out[32]);

#endif /* NX_SHA256_H */
