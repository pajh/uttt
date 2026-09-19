#ifndef UTTT_SUPPORT_H
#define UTTT_SUPPORT_H

#include <stdint.h>
#include <stdbool.h>

/* Shared generic support belongs here, including approved generic assertions. */
typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;

#if defined(BOARD_ASSERTS) && BOARD_ASSERTS
#include <stdio.h>
#include <stdlib.h>

static inline void support_assert_fail(const char *expression,
                                       const char *file, int line)
{
    fprintf(stderr, "assertion failed: %s (%s:%d)\n", expression, file, line);
    abort();
}

/* Assertions must be passed side-effect-free conditions. */
#define ASSERT(condition) do { \
    if (!(condition)) support_assert_fail(#condition, __FILE__, __LINE__); \
} while (0)
#else
#define ASSERT(condition) ((void)0)
#endif

#endif /* UTTT_SUPPORT_H */
