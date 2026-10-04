/* ============================================================================
 * logger.h -- In-memory activity log.
 * Implementation lands in PHASE 2.
 * ============================================================================
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stddef.h>

#define LOG_MAX_ENTRIES 256
#define LOG_MSG_MAX     512

/* Initialize the logger. Must be called once at startup. */
void logger_init(void);

/* Append a printf-style formatted entry with the current timestamp. */
void logger_append(const char *fmt, ...);

/* Copy up to `max` most-recent entries into `out_buf` as
 * newline-separated lines. Each line is prefixed with "[YYYY-MM-DD HH:MM:SS]".
 * Returns the number of bytes written (excluding the trailing NUL). */
size_t logger_recent(char *out_buf, size_t out_buf_size, int max);

/* Drop all entries. */
void logger_clear(void);

#endif /* LOGGER_H */