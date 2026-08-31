/*
 * FNV-1a, 64 bit.
 *
 * Vectors use string keys for policies where the key is opaque, but the C API
 * takes uint64_t keys and leaves hashing to the caller. The C vector generator
 * maps those strings through this function and compares against the mapped
 * expectation, so the same vectors.json drives C without a JSON parser
 * (concept.md §12.2).
 *
 * This is a key-mapping convenience, not the registry's hash function: policies
 * that hash keys internally use pb_mix32 (see rng.h).
 */

#ifndef POLICYBOOK_HASH_H
#define POLICYBOOK_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PB_FNV1A64_OFFSET 0xcbf29ce484222325ULL
#define PB_FNV1A64_PRIME 0x100000001b3ULL

/* Hash `len` bytes of `data`. */
static inline uint64_t pb_fnv1a64(const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint64_t hash = PB_FNV1A64_OFFSET;
    size_t i;

    for (i = 0; i < len; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= PB_FNV1A64_PRIME;
    }
    return hash;
}

/* Hash a NUL-terminated string. */
static inline uint64_t pb_fnv1a64_str(const char *text)
{
    uint64_t hash = PB_FNV1A64_OFFSET;

    while (*text != '\0') {
        hash ^= (uint64_t)(unsigned char)*text++;
        hash *= PB_FNV1A64_PRIME;
    }
    return hash;
}

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_HASH_H */
