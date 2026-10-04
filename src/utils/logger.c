/* ============================================================================
 * logger.c -- In-memory ring buffer of activity log lines.
 *
 * Thread-safety: not thread-safe. The application is single-threaded.
 *
 * Design:
 *   - Bounded ring buffer of LOG_MAX_ENTRIES strings.
 *   - Each line is timestamped using localtime + strftime.
 *   - The most recent line replaces the oldest when the buffer fills up.
 * ============================================================================
 */

#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Internal entry: a single log line (timestamp + message). */
typedef struct {
    char msg[LOG_MSG_MAX];
} LogEntry;

/* Module-private state. */
static LogEntry g_log[LOG_MAX_ENTRIES];
static int      g_count = 0;      /* number of valid entries, <= LOG_MAX_ENTRIES */
static int      g_next  = 0;      /* index where the NEXT entry will be written */

void
logger_init(void)
{
    g_count = 0;
    g_next  = 0;
    for (int i = 0; i < LOG_MAX_ENTRIES; i++) {
        g_log[i].msg[0] = '\0';
    }
}

void
logger_append(const char *fmt, ...)
{
    if (fmt == NULL) return;

    /* Build the formatted message. Truncate body if needed so the final
     * "[timestamp] <body>" always fits within LOG_MSG_MAX. The 24-byte
     * reserve covers "[YYYY-MM-DD HH:MM:SS] " (max ~22 chars) + NUL. */
    char body[LOG_MSG_MAX - 24];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    /* Compose the full entry as "[YYYY-MM-DD HH:MM:SS] body". */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    snprintf(g_log[g_next].msg, LOG_MSG_MAX, "[%s] %s", ts, body);

    /* Advance ring pointer. */
    g_next = (g_next + 1) % LOG_MAX_ENTRIES;
    if (g_count < LOG_MAX_ENTRIES) g_count++;
}

size_t
logger_recent(char *out_buf, size_t out_buf_size, int max)
{
    if (out_buf == NULL || out_buf_size == 0) return 0;

    if (max <= 0 || max > g_count) max = g_count;

    /* The most recent entry is at (g_next - 1) mod LOG_MAX_ENTRIES. We walk
     * backwards `max` steps to collect the latest N entries in order. */
    int start = (g_next - max + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;

    size_t written = 0;
    for (int i = 0; i < max; i++) {
        int idx = (start + i) % LOG_MAX_ENTRIES;
        const char *line = g_log[idx].msg;

        size_t len = strlen(line);
        if (written + len + 2 > out_buf_size) {
            break;   /* buffer full - stop */
        }
        memcpy(out_buf + written, line, len);
        written += len;
        out_buf[written++] = '\n';
    }
    out_buf[written] = '\0';
    return written;
}

void
logger_clear(void)
{
    logger_init();
}