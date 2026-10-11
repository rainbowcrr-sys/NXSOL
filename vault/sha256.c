/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- SHA-256 (compact, self-contained)
 * Copyright (C) 2026 rainbowcrr-sys
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Why this is here rather than linked from libcrypto: the vault's integrity
 * check has to work in the initramfs, where there is no OpenSSL and no
 * dynamic linker to speak of. A sha256 that fits in one file and depends on
 * nothing is worth more here than one that is faster.
 *
 * Standard FIPS 180-4 implementation.
 */

#include "sha256.h"
#include <stdlib.h>
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void compress(uint32_t h[8], const uint8_t block[64])
{
    uint32_t w[64], a, b, c, d, e, f, g, hh, t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];

    for (i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);

        t1 = hh + S1 + ch + K[i] + w[i];
        t2 = S0 + maj;
        hh = g; g = f; f = e;
        e = d + t1;
        d = c; c = b; b = a;
        a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void nx_sha256(const void *data, size_t len, uint8_t out[32])
{
    static const uint32_t h0[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    uint32_t h[8];
    const uint8_t *p = (const uint8_t *)data;
    size_t n = len;
    uint8_t tail[128];
    size_t tail_len;
    uint64_t bits;
    int i;

    for (i = 0; i < 8; i++) h[i] = h0[i];

    while (n >= 64) {
        compress(h, p);
        p += 64;
        n -= 64;
    }

    /* Padding: 0x80, zeros, then the bit length big-endian. */
    tail_len = n;
    for (i = 0; i < (int)tail_len; i++) tail[i] = p[i];
    tail[tail_len++] = 0x80;
    while (tail_len % 64 != 56) tail[tail_len++] = 0;

    bits = (uint64_t)len * 8;
    for (i = 7; i >= 0; i--) tail[tail_len++] = (uint8_t)(bits >> (i * 8));

    for (i = 0; i < (int)tail_len; i += 64) compress(h, tail + i);

    for (i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

/* HMAC-SHA256. The key is the vault's own signing key; without it a tampered
 * file does not produce a matching tag. */
void nx_hmac_sha256(const void *key, size_t key_len,
                    const void *data, size_t data_len,
                    uint8_t out[32])
{
    uint8_t ipad[64], opad[64], inner[32], tmp[64];
    uint8_t *buf;
    size_t i;

    for (i = 0; i < 64; i++) { ipad[i] = 0x36; opad[i] = 0x5c; }

    if (key_len > 64) {
        nx_sha256(key, key_len, tmp);
        key = tmp;
        key_len = 32;
    }
    for (i = 0; i < key_len; i++) {
        ipad[i] ^= ((const uint8_t *)key)[i];
        opad[i] ^= ((const uint8_t *)key)[i];
    }

    buf = (uint8_t *)malloc(64 + data_len);
    if (!buf) { memset(out, 0, 32); return; }
    memcpy(buf, ipad, 64);
    memcpy(buf + 64, data, data_len);
    nx_sha256(buf, 64 + data_len, inner);
    free(buf);

    buf = (uint8_t *)malloc(64 + 32);
    if (!buf) { memset(out, 0, 32); return; }
    memcpy(buf, opad, 64);
    memcpy(buf + 64, inner, 32);
    nx_sha256(buf, 64 + 32, out);
    free(buf);
}
