#ifndef MS_PLATFORM_H
#define MS_PLATFORM_H

#include <stdbool.h>
#include <stdio.h>

#ifdef _WIN32
  #define MS_PLATFORM_WINDOWS 1
#else
  #define MS_PLATFORM_WINDOWS 0
#endif

#ifdef __linux__
  #define MS_PLATFORM_LINUX 1
#else
  #define MS_PLATFORM_LINUX 0
#endif

#ifdef __APPLE__
  #define MS_PLATFORM_MACOS 1
#else
  #define MS_PLATFORM_MACOS 0
#endif

#if MS_PLATFORM_WINDOWS
  #define MS_PATH_SEPARATOR '\\'
  #define MS_PATH_SEPARATOR_STR "\\"
#else
  #define MS_PATH_SEPARATOR '/'
  #define MS_PATH_SEPARATOR_STR "/"
#endif

bool ms_platform_file_exists(const char* path);
char* ms_platform_read_file(const char* path);
bool ms_platform_write_file(const char* path, const char* content);
char* ms_platform_get_cwd(void);
char* ms_platform_join_path(const char* a, const char* b);
char* ms_platform_get_executable_path(void);
double ms_platform_get_time_seconds(void);
char* ms_platform_get_env(const char* name);

void ms_platform_enable_console_colors(void);
bool ms_platform_supports_colors(void);
void ms_platform_set_console_color(const char* ansi_code);
void ms_platform_reset_console_color(void);

#endif
