#ifndef MLOG_LOG_H_
#define MLOG_LOG_H_

#if defined(__cplusplus)
extern "C" {
#endif

#define MLOG_API __attribute__((visibility("default")))

#define MLOG_Log(log_level, ...) \
        MLOG_Log_(log_level, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define MLOG_Assert(Expr, ...) \
        MLOG_Assert_(#Expr, Expr, __FILE__, __func__, __LINE__, __VA_ARGS__)

#ifdef MLOG_DEBUG
#define MLOG_DBG_Assert(...) MLOG_Assert(__VA_ARGS__)
#define MLOG_DBG_Log(...) MLOG_Log(__VA_ARGS__)
#else
#define MLOG_DBG_Assert(...)
#define MLOG_DBG_Log(...)
#endif

enum MLOG_API MLOG_log_level_t {
    MLOG_LOG_NONE = 0,
    MLOG_LOG_WARN,
    MLOG_LOG_TRACE,
    MLOG_LOG_INFO,
    MLOG_LOG_DEBUG,
    MLOG_LOG_MAX
};

MLOG_API
extern int MLOG_Init();

MLOG_API
void MLOG_Assert_(const char *expr_str, int expr, const char *file,
                 const char *func, int line, const char *format, ...)
        __attribute__((__format__(__printf__, 6, 7)));

MLOG_API
void MLOG_Log_(enum MLOG_log_level_t log_level, const char *file,
              const char *func, int line, const char *format, ...)
        __attribute__((__format__(__printf__, 5, 6)));

#if defined(__cplusplus)
}
#endif

#endif // MLOG_LOG_H_
