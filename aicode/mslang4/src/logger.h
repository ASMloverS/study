#ifndef MS_LOGGER_H
#define MS_LOGGER_H

#include <stdbool.h>
#include <stdio.h>

#ifdef MS_LOG_LEVEL_TRACE
  #define MS_LOG_LEVEL 0
#elif defined(MS_LOG_LEVEL_DEBUG)
  #define MS_LOG_LEVEL 1
#elif defined(MS_LOG_LEVEL_INFO)
  #define MS_LOG_LEVEL 2
#elif defined(MS_LOG_LEVEL_WARN)
  #define MS_LOG_LEVEL 3
#elif defined(MS_LOG_LEVEL_ERROR)
  #define MS_LOG_LEVEL 4
#else
  #define MS_LOG_LEVEL 2
#endif

typedef enum {
	MS_LOG_TRACE,
	MS_LOG_DEBUG,
	MS_LOG_INFO,
	MS_LOG_WARN,
	MS_LOG_ERROR,
	MS_LOG_FATAL,
	MS_LOG_OFF
} MsLogLevel;

void ms_logger_set_level(MsLogLevel level);
void ms_logger_set_output(FILE *stream);
void ms_logger_enable_colors(bool enable);
void ms_logger_enable_timestamp(bool enable);
void ms_logger_log(MsLogLevel level, const char *file, int line,
		   const char *func, const char *fmt, ...);

#define ms_logger_trace(...)                                                   \
	ms_logger_log(MS_LOG_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ms_logger_debug(...)                                                   \
	ms_logger_log(MS_LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ms_logger_info(...)                                                    \
	ms_logger_log(MS_LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ms_logger_warn(...)                                                    \
	ms_logger_log(MS_LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ms_logger_error(...)                                                   \
	ms_logger_log(MS_LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ms_logger_fatal(...)                                                   \
	ms_logger_log(MS_LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif
