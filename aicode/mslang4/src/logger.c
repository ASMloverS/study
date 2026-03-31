#include "logger.h"
#include "platform.h"
#include <stdarg.h>
#include <string.h>
#include <time.h>

static MsLogLevel current_level = MS_LOG_INFO;
static FILE *output_stream = NULL;
static bool colors_enabled = false;
static bool timestamps_enabled = false;

void ms_logger_set_level(MsLogLevel level)
{
	current_level = level;
}

void ms_logger_set_output(FILE *stream)
{
	output_stream = stream;
}

void ms_logger_enable_colors(bool enable)
{
	colors_enabled = enable;
}

void ms_logger_enable_timestamp(bool enable)
{
	timestamps_enabled = enable;
}

static const char *level_tag(MsLogLevel level)
{
	switch (level) {
	case MS_LOG_TRACE:
		return "TRACE";
	case MS_LOG_DEBUG:
		return "DEBUG";
	case MS_LOG_INFO:
		return "INFO";
	case MS_LOG_WARN:
		return "WARN";
	case MS_LOG_ERROR:
		return "ERROR";
	case MS_LOG_FATAL:
		return "FATAL";
	case MS_LOG_OFF:
		return "OFF";
	}
	return "UNKNOWN";
}

static const char *level_ansi(MsLogLevel level)
{
	switch (level) {
	case MS_LOG_TRACE:
		return "\033[90m";
	case MS_LOG_DEBUG:
		return "\033[34m";
	case MS_LOG_INFO:
		return "\033[32m";
	case MS_LOG_WARN:
		return "\033[33m";
	case MS_LOG_ERROR:
		return "\033[31m";
	case MS_LOG_FATAL:
		return "\033[35m";
	case MS_LOG_OFF:
		return "";
	}
	return "";
}

void ms_logger_log(MsLogLevel level, const char *file, int line,
		   const char *func, const char *fmt, ...)
{
	if (level < current_level)
		return;

	FILE *out = output_stream ? output_stream : stderr;

	if (colors_enabled)
		fprintf(out, "%s", level_ansi(level));

	if (timestamps_enabled) {
		time_t now = time(NULL);
		struct tm *t = localtime(&now);
		fprintf(out, "[%02d:%02d:%02d] ", t->tm_hour, t->tm_min,
			t->tm_sec);
	}

	fprintf(out, "[%s] %s:%d@%s - ", level_tag(level), file, line, func);

	va_list args;
	va_start(args, fmt);
	vfprintf(out, fmt, args);
	va_end(args);

	fprintf(out, "\n");

	if (colors_enabled)
		fprintf(out, "\033[0m");
}
