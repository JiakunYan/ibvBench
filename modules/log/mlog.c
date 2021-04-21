#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mlog.h"

static const char * const log_levels[] = {
    [MLOG_LOG_WARN] = "warn",
    [MLOG_LOG_TRACE] = "trace",
    [MLOG_LOG_INFO] = "info",
    [MLOG_LOG_DEBUG] = "debug",
    [MLOG_LOG_MAX] = NULL
};

static int MLOG_LOG_LEVEL = MLOG_LOG_WARN;

int MLOG_Init()  {
    char *p = getenv("MLOG_LOG_LEVEL");
    if (p == NULL) ;
    else if (strcmp(p, "none") == 0 || strcmp(p, "NONE") == 0)
        MLOG_LOG_LEVEL = MLOG_LOG_NONE;
    else if (strcmp(p, "warn") == 0 || strcmp(p, "WARN") == 0)
        MLOG_LOG_LEVEL = MLOG_LOG_WARN;
    else if (strcmp(p, "trace") == 0 || strcmp(p, "TRACE") == 0)
        MLOG_LOG_LEVEL = MLOG_LOG_TRACE;
    else if (strcmp(p, "info") == 0 || strcmp(p, "INFO") == 0)
        MLOG_LOG_LEVEL = MLOG_LOG_INFO;
    else if (strcmp(p, "debug") == 0 || strcmp(p, "DEBUG") == 0)
        MLOG_LOG_LEVEL = MLOG_LOG_DEBUG;
    else if (strcmp(p, "max") == 0 || strcmp(p, "MAX") == 0)
        MLOG_LOG_LEVEL = MLOG_LOG_MAX;
    else
        MLOG_Log(MLOG_LOG_WARN, "unknown env MLOG_LOG_LEVEL (%s against none|warn|trace|info|debug|max). use the default MLOG_LOG_WARN.\n", p);
    return MLOG_LOG_LEVEL;
}

void MLOG_Assert_(const char *expr_str, int expr, const char *file,
                 const char *func, int line, const char *format, ...) {
  char buf[1024];
  int size;
  va_list vargs;

  if (!expr) {
    size = snprintf(buf, sizeof(buf), "%d:%s:%s:%d<Assert failed: %s> ", getpid(), file, func,
                    line, expr_str);

    va_start(vargs, format);
    vsnprintf(buf + size, sizeof(buf) - size, format, vargs);
    va_end(vargs);

    fprintf(stderr, "%s", buf);
    abort();
  }
}

void MLOG_Log_(enum MLOG_log_level_t log_level, const char *file,
              const char *func, int line, const char *format, ...) {
  char buf[1024];
  int size;
  va_list vargs;
  MLOG_Assert(log_level != MLOG_LOG_NONE, "You should not use MLOG_LOG_NONE!\n");
  if (log_level <= MLOG_LOG_LEVEL) {
    size = snprintf(buf, sizeof(buf), "%d:%s:%s:%d<%s> ", getpid(), file, func,
                    line, log_levels[log_level]);

    va_start(vargs, format);
    vsnprintf(buf + size, sizeof(buf) - size, format, vargs);
    va_end(vargs);

    fprintf(stderr, "%s", buf);
  }
}
