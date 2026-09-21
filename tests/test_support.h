#ifndef UTTT_TEST_SUPPORT_H
#define UTTT_TEST_SUPPORT_H

/*
 * Header-only test support.
 *
 * Phase 1 provides only the two primitives every blob/hash vector needs:
 * a 64-bit FNV-1a hash of a memory block and a binary save-to-disk helper.
 * No dependency on the board engine or on any test runner.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Standard FNV-1a 64-bit parameters.  The Python second authority must use
 * exactly these constants and the same byte order. */
#define TEST_FNV_OFFSET UINT64_C(0xcbf29ce484222325)
#define TEST_FNV_PRIME UINT64_C(0x100000001b3)

/* Hash len bytes starting at data.  Byte order is simply memory order, so a
 * blob is hashed identically on any host that stores its fields the same way;
 * blobs are written little-endian field by field (no struct memcpy). */
static inline uint64_t test_hash64(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = TEST_FNV_OFFSET;

    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= TEST_FNV_PRIME;
    }
    return hash;
}

/* Append a little-endian 16-bit value at dst and return dst + 2.  Blobs are
 * packed as fixed-width little-endian fields so a Python authority can build
 * byte-identical output. */
static inline uint8_t *test_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    return dst + 2;
}

/* Create every directory component of path, like mkdir -p.  Returns 1 on
 * success (including already-existing directories), 0 on error. */
static inline int test_mkdir_p(const char *path)
{
    char buffer[512];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof buffer)
        return 0;
    memcpy(buffer, path, len + 1);
    if (buffer[len - 1] == '/')
        buffer[len - 1] = '\0';

    for (char *p = buffer + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0777) != 0 && errno != EEXIST)
                return 0;
            *p = '/';
        }
    }
    if (mkdir(buffer, 0777) != 0 && errno != EEXIST)
        return 0;
    return 1;
}

/* Write the len bytes at data to path, creating parent directories.  Returns
 * 1 on success and 0 on any failure. */
static inline int test_save_blob(const char *path, const void *data, size_t len)
{
    char directory[512];
    size_t path_len = strlen(path);
    FILE *file;
    size_t written;
    int closed;

    if (path_len == 0 || path_len >= sizeof directory)
        return 0;
    memcpy(directory, path, path_len + 1);
    char *slash = strrchr(directory, '/');
    if (slash != NULL) {
        *slash = '\0';
        if (!test_mkdir_p(directory))
            return 0;
    }

    file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    written = (len == 0) ? 0 : fwrite(data, 1, len, file);
    closed = fclose(file);
    return written == len && closed == 0;
}

#endif /* UTTT_TEST_SUPPORT_H */
