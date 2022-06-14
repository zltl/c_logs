/**
 * @file log.h
 * @brief logging functions and macros
 * @details This file contains the logging functions and macros,. All functions
 * in this file are thread-safe. Use example:
 * @code
 * LOG_INFO("Hello world!");
 * @endcode
 */

#ifndef LOG_H_
#define LOG_H_

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * log levels
 */

#define LOG_LEVEL_TRACE 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_WARN 3
#define LOG_LEVEL_ERROR 4
#define LOG_LEVEL_CRITICAL 5
#define LOG_LEVEL_OFF 6

/*
 * log file split scheme use by log_set_file()
 */

#define LOG_FILE_ROTATE_NONE 0
#define LOG_FILE_ROTATE_HOURLY 1
#define LOG_FILE_ROTATE_DAYLY 2

/**
 * @brief Initialize the logging system, call this function before using any
 * other logging function, or just ignore it.
 */
void log_init();
/**
 * @brief Release all resouces allocated by the logging system, call this
 * function when you are done using the logging system.
 */
void log_close();

/**
 * @brief set the log level, the default log level is LOG_LEVEL_TRACE.
 * logsystem will only log messages with level equal or higher than the
 * specified level.
 *
 * @param level the log level, see LOG_LEVEL_*
 * @return int 0 on success, -1 on error
 */
int log_set_level(int level);
/**
 * @brief set the log level by string: "trace", "debug", "info", "warn",
 * "error", "critical", "off".
 *
 * @param level the log level string
 * @return int 0 on success, -1 on error
 */
int log_set_level_str(const char* level);

/**
 * @brief Set the output file for the logging system.
 *
 * @param path directory path of the log file
 * @param filename  name prefix of the log file, the real file name will be
 * filename_YYYY-MM-DD.log
 * @param rotate the log file split scheme, see LOG_FILE_ROTATE_*
 * @param file_size the max total size of the log files
 * @param file_num the max number of the log file
 * @return int
 */
int log_set_file(const char* path, const char* filename, int rotate,
                 long file_size, int file_num);

#ifdef ROOT_DIR
#define PROJECT_ROOT_PATH_NAME ROOT_DIR
#define __FILENAME__                                \
    (strstr(__FILE__, PROJECT_ROOT_PATH_NAME)       \
         ? strstr(__FILE__, PROJECT_ROOT_PATH_NAME) \
         : __FILE__)
#else
#define __FILENAME__ __FILE__
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"

#define FUNC_NAME __func__

int log_default_printf(const int level, const char* file, const int line,
                       const char* func, const char* fmt, ...);

#define LOG_PRINTF(level, ...) \
    log_default_printf(level, __FILENAME__, __LINE__, FUNC_NAME, __VA_ARGS__)

/**
 * @brief Logging macros
 *
 * supported formats:
 *
 *   - %[0][width]T              time_t
 *   - %[0][width][u][x|X]z      ssize_t/size_t
 *   - %[0][width][u][x|X]d      int/u_int
 *   - %[0][width][u][x|X]l      long
 *   - %[0][width][u][x|X]D      int32_t/uint32_t
 *   - %[0][width][u][x|X]L      int64_t/uint64_t
 *   - %[0][width][.width]f      double, max valid number fits to %18.15f
 *   - %p                        void *
 *   - %S                        sstr_t
 *   - %s                        null-terminated string
 *   - %*s                       length and string
 *   - %Z                        '\0'
 *   - %N                        '\n'
 *   - %c                        char
 *   - %%                        %
 *
 *  reserved:
 *   - %C                        wchar
 *
 *  if %u/%x/%X, tailing d can be ignore
 */

#define TRACEF(...) LOG_PRINTF(LOG_LEVEL_TRACE, __VA_ARGS__)
#define DEBUGF(...) LOG_PRINTF(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define INFOF(...) LOG_PRINTF(LOG_LEVEL_INFO, __VA_ARGS__)
#define WARNF(...) LOG_PRINTF(LOG_LEVEL_WARN, __VA_ARGS__)
#define ERRORF(...) LOG_PRINTF(LOG_LEVEL_ERROR, __VA_ARGS__)
#define CRITICALF(...) LOG_PRINTF(LOG_LEVEL_CRITICAL, __VA_ARGS__)

#pragma GCC diagnostic pop

#ifdef __cplusplus
}
#endif

#endif /* LOG_H_ */
