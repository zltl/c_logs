#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "log.h"

#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "sstr.h"

/// max length of datetime, level, filename and function-names
#define LOG_MAX_PREFIX_SIZE 512

/**
 * @brief contain message to be log.
 */
struct log_source_s {
    int level;
    sstr_t buf;
};

/**
 * @brief define the logging destination.
 */
struct log_sink_s {
    int level;
    pthread_mutex_t mutex;
    int log_dest;
    int split_scheme;
    time_t prev_split_timestamp;
    sstr_t dir_path;
    int fd;
    sstr_t filename_prefix;
    sstr_t filename_current;
    int log_file_total_limit;
    long log_file_total_bytes_limit;
};

typedef struct log_sink_s log_sink_t;
typedef struct log_source_s log_source_t;

log_source_t* log_source_new() {
    log_source_t* s = (log_source_t*)malloc(sizeof(log_source_t));
    s->level = LOG_LEVEL_TRACE;
    s->buf = sstr_new();
    return s;
}

void log_source_free(log_source_t* s) {
    if (s) {
        sstr_free(s->buf);
    }
    free(s);
}

// 1937-01-01T12:00:27.870000+00:20
int log_source_set_timestamp_level(log_source_t* source, int level) {
    struct timeval tv;
    struct tm tm_info;
    char ts_buf[43];
    char ts_usec[10];
    size_t size = 0;
    source->level = level;
    static char const* severity_str[] = {"trace ", "debug ", "info  ", "warn  ",
                                         "error ", "criti ", "OFF   "};

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_info);
    size = strftime(ts_buf, sizeof(ts_buf), "%FT%T.000000%z ", &tm_info);
    // ts_buf=1937-01-01T12:00:27.000000+08:00

    // set zero useconds
    int useclen = snprintf(ts_usec, sizeof(ts_usec), "%06ld", tv.tv_usec);
    if (useclen > (int)sizeof(ts_buf) - 20) {
        useclen = (int)sizeof(ts_buf) - 20;
    }
    memcpy(ts_buf + 20, ts_usec, useclen);
    sstr_append_of(source->buf, ts_buf, size);

    // set level
    if (level < LOG_LEVEL_TRACE || level >= LOG_LEVEL_OFF) {
        return -1;
    }
    sstr_append_of(source->buf, severity_str[level], 6);
    return 0;
}

int log_source_set_file_func(log_source_t* source, const char* file, int line,
                             const char* func) {
    char file_line_func[LOG_MAX_PREFIX_SIZE];
    snprintf(file_line_func, sizeof(file_line_func), "%s+%d:%s ", file, line,
             func);
    sstr_append_of(source->buf, file_line_func, strlen(file_line_func));
    return 0;
}

int log_source_set_kv(log_source_t* source, const char* k, const char* v) {
    char kv[LOG_MAX_PREFIX_SIZE];
    snprintf(kv, sizeof(kv), "%s=%s", k, v);
    sstr_append_of(source->buf, kv, strlen(kv));
    return 0;
}

int log_source_set_msg_vsprintf(log_source_t* source, const char* fmt,
                                va_list args) {
    sstr_t s = sstr_vslprintf(fmt, args);
    sstr_append(source->buf, s);
    sstr_free(s);
    return 0;
}

static log_sink_t* sink() {
    static log_sink_t sink_instance;
    return &sink_instance;
}

#define LOG_DEST_STDOUT 0
#define LOG_DEST_FILE 1

static void log_sink_init(log_sink_t* sink) {
    sink->level = LOG_LEVEL_TRACE;
    pthread_mutex_init(&sink->mutex, NULL);
    sink->log_dest = LOG_DEST_STDOUT;  // stdout
    sink->split_scheme = LOG_FILE_ROTATE_NONE;
    sink->prev_split_timestamp = 0;
    sink->dir_path = sstr("./log");
    sink->fd = 1;
    sink->filename_prefix = sstr("log");
    sink->filename_current = sstr("log_current");
    sink->log_file_total_limit = 10;
    sink->log_file_total_bytes_limit = 1024 * 1024 * 1024; /* 1Gi */
}

void log_sink_close(log_sink_t* sink) {
    pthread_mutex_lock(&sink->mutex);
    if (sink->log_dest != LOG_DEST_STDOUT && sink->fd != 1) {
        close(sink->fd);
        sink->fd = -1;
    }
    sstr_free(sink->dir_path);
    sstr_free(sink->filename_prefix);
    sstr_free(sink->filename_current);
    pthread_mutex_unlock(&sink->mutex);
}

int log_sink_set_level(log_sink_t* sink, int level) {
    pthread_mutex_lock(&sink->mutex);
    sink->level = level;
    pthread_mutex_unlock(&sink->mutex);
    return 0;
}

int log_sink_set_file(log_sink_t* s, const char* path,
                      const char* filename_prefix, int rotate, long file_size,
                      int file_num) {
    pthread_mutex_lock(&s->mutex);
    s->log_dest = LOG_DEST_FILE;
    s->split_scheme = rotate;
    s->log_file_total_bytes_limit = file_size;
    s->log_file_total_limit = file_num;
    sstr_free(s->filename_prefix);
    s->filename_prefix = sstr(filename_prefix);
    sstr_free(s->dir_path);
    s->dir_path = sstr(path);
    pthread_mutex_unlock(&s->mutex);
    return 0;
}

/* for logfile limit */
static int filter_fn(const struct dirent* dir) {
    if (dir->d_type != DT_REG && dir->d_type != DT_UNKNOWN) return 0;
    return (strcmp(dir->d_name, ".") != 0 || strcmp(dir->d_name, "..") != 0);
}

int compar(const struct dirent** dir1, const struct dirent** dir2) {
    struct stat buf1;
    struct stat buf2;
    time_t mtime1;
    time_t mtime2;
    char* log_dirent_name = (char*)sstr_cstr(sink()->dir_path);
    char filename[FILENAME_MAX];
    snprintf(filename, sizeof(filename), "%s/%s", log_dirent_name,
             (*dir1)->d_name);
    stat(filename, &buf1);
    snprintf(filename, sizeof(filename), "%s/%s", log_dirent_name,
             (*dir2)->d_name);
    stat(filename, &buf2);
    mtime1 = buf1.st_mtime;
    mtime2 = buf2.st_mtime;
    return (int)(mtime1 - mtime2);
}

static int logfile_limit(log_sink_t* sink) {
    int logfile_count = 0;
    long long logfile_size = 0;
    int tmpcount = 0;
    struct stat buf;
    struct dirent** namelist = NULL;
    int ret = 0;
    int i;
    char filename[LOG_MAX_PREFIX_SIZE];
    if ((logfile_count = scandir(sstr_cstr(sink->dir_path), &namelist,
                                 filter_fn, compar)) < 0) {
        return -1;
    }

    if (logfile_count <= 0) {
        ret = 0;
        goto cleanup;
    }

    for (i = 0; i < logfile_count; i++) {
        snprintf(filename, sizeof(filename), "%s/%s", sstr_cstr(sink->dir_path),
                 namelist[i]->d_name);
        if (stat(filename, &buf) < 0) {
            ret = -1;
            goto cleanup;
        }
        logfile_size += buf.st_size;
    }

    i = 0;
    tmpcount = logfile_count;
    while (tmpcount > sink->log_file_total_limit ||
           logfile_size > sink->log_file_total_bytes_limit) {
        snprintf(filename, sizeof(filename), "%s/%s", sstr_cstr(sink->dir_path),
                 namelist[i]->d_name);
        if (stat(filename, &buf) < 0) {
            ret = -1;
            goto cleanup;
        }
        if (remove(filename) < 0) {
            ret = -1;
            goto cleanup;
        }
        logfile_size -= buf.st_size;
        i++;
        tmpcount--;
    }

cleanup:
    i = 0;
    if (namelist) {
        while (i < logfile_count) {
            free(namelist[i]);
            i++;
        }
        free(namelist);
    }
    return ret;
}

static int log_sink_update(log_sink_t* sink) {
    struct timeval tv;
    struct tm curtm;
    struct tm prevtm;
    char ts_buf[40];
    char new_file_path[LOG_MAX_PREFIX_SIZE];
    size_t ts_buf_size;

    if (sink->log_dest == LOG_DEST_STDOUT) {
        return 0;
    }

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &curtm);
    localtime_r(&sink->prev_split_timestamp, &prevtm);

    int open_new_file = 0;

    if (sink->split_scheme == LOG_FILE_ROTATE_NONE) {
        if (sink->fd <= 1) {
            open_new_file = 1;
            ts_buf[0] = '\0';
        }
    } else {
        if (sink->split_scheme == LOG_FILE_ROTATE_DAYLY) {
            if (curtm.tm_yday != prevtm.tm_yday ||
                curtm.tm_year != prevtm.tm_year) {
                open_new_file = 1;
                ts_buf_size = strftime(ts_buf, sizeof(ts_buf), "%FT", &curtm);
                ts_buf[ts_buf_size] = '\0';
            }
        } else if (sink->split_scheme == LOG_FILE_ROTATE_HOURLY) {
            if (curtm.tm_hour != prevtm.tm_hour ||
                curtm.tm_yday != prevtm.tm_yday ||
                curtm.tm_year != prevtm.tm_year) {
                open_new_file = 1;
                ts_buf_size = strftime(ts_buf, sizeof(ts_buf), "%FT%H", &curtm);
                ts_buf[ts_buf_size] = '\0';
            }
        }
    }
    if (open_new_file) {                
        pthread_mutex_lock(&sink->mutex);
        if (logfile_limit(sink) < 0) {
            pthread_mutex_unlock(&sink->mutex);
            return -1;
        }

        snprintf(new_file_path, sizeof(new_file_path), "%s/%s.%s",
                 sstr_cstr(sink->dir_path), sstr_cstr(sink->filename_prefix),
                 ts_buf);
        int of = sink->fd;        
        sink->fd = open(new_file_path, O_WRONLY | O_APPEND | O_CREAT, 00755);
        if (sink->fd < 0) {            
            sink->fd = of;
            pthread_mutex_unlock(&sink->mutex);
            return -1;
        } else {                        
            if (of > 1) {
                close(of);
            }
        }
        sink->prev_split_timestamp = tv.tv_sec;
        pthread_mutex_unlock(&sink->mutex);
    }
    return 0;
}

int log_sink_write(log_sink_t* sink, log_source_t* source) {
    if (source->level >= sink->level) {
        log_sink_update(sink);
        sstr_append_of(source->buf, "\n", 1);
        write(sink->fd, sstr_cstr(source->buf), sstr_length(source->buf));
    }
    fsync(sink->fd);

    return 0;
}

int log_default_printf(const int level, const char* file, const int line,
                       const char* func, const char* fmt, ...) {
    va_list args;
    int r;
    if (sink()->level > level) {
        return 0;
    }
    log_source_t* s = log_source_new();
    log_source_set_timestamp_level(s, level);
    log_source_set_file_func(s, file, line, func);

    va_start(args, fmt);
    r = log_source_set_msg_vsprintf(s, fmt, args);
    va_end(args);
    log_sink_write(sink(), s);
    log_source_free(s);
    return r;
}

int log_set_level(int level) {
    INFOF("set log level=%d", level);
    // we not lock it, and accepted the risk of race.
    sink()->level = level;
    return 0;
}

int log_set_level_str(const char* level) {
    int lev = LOG_LEVEL_TRACE;
    if (strcmp(level, "debug") == 0) {
        lev = LOG_LEVEL_DEBUG;
    } else if (strcmp(level, "info") == 0) {
        lev = LOG_LEVEL_INFO;
    } else if (strcmp(level, "warn") == 0) {
        lev = LOG_LEVEL_WARN;
    } else if (strcmp(level, "error") == 0) {
        lev = LOG_LEVEL_ERROR;
    } else if (strcmp(level, "critical") == 0) {
        lev = LOG_LEVEL_CRITICAL;
    } else if (strcmp(level, "off") == 0) {
        lev = LOG_LEVEL_OFF;
    } else if (strlen(level) != 0 && strcmp(level, "none") == 0) {
        lev = LOG_LEVEL_OFF + 1;
    }
    return log_set_level(lev);
}

int log_set_file(const char* path, const char* filename, int rotate,
                 long file_size, int file_num) {
    log_sink_set_file(sink(), path, filename, rotate, file_size, file_num);
    return 0;
}

void log_init() { log_sink_init(sink()); }
void log_close() { log_sink_close(sink()); }
