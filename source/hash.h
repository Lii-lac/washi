// SHA-256 (for PIN hashing) and CRC32 (for backup verification). No platform dependencies.
#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} Sha256Ctx;

void sha256_init(Sha256Ctx *c);
void sha256_update(Sha256Ctx *c, const void *data, size_t len);
void sha256_final(Sha256Ctx *c, uint8_t out[32]);
void sha256_hex(const void *data, size_t len, char out[65]);

uint32_t crc32_update(uint32_t crc, const void *data, size_t len); // start with 0

#endif
