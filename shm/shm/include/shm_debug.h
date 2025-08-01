#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <sys/time.h>
#include <time.h> 

// Debug levels
#define SHM_LOG_LEVEL_ERROR   0
#define SHM_LOG_LEVEL_INFO    1
#define SHM_LOG_LEVEL_DEBUG   2
#define SHM_LOG_LEVEL_DETAIL  3

// Define the active debug level
#define ACTIVE_LOG_LEVEL SHM_LOG_LEVEL_INFO

// Define debug level flags
#define LOG_ERROR_ENABLED   (ACTIVE_LOG_LEVEL >= SHM_LOG_LEVEL_ERROR)
#define LOG_INFO_ENABLED    (ACTIVE_LOG_LEVEL >= SHM_LOG_LEVEL_INFO)
#define LOG_DEBUG_ENABLED   (ACTIVE_LOG_LEVEL >= SHM_LOG_LEVEL_DEBUG)
#define LOG_DETAIL_ENABLED  (ACTIVE_LOG_LEVEL >= SHM_LOG_LEVEL_DETAIL)

// Print log with error level
#define LOG_ERROR(func, ...) \
    do { if (LOG_ERROR_ENABLED) { \
        fprintf(stderr, "[ERROR] %s: ", func); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } } while (0)

// Print log with info level
#define LOG_INFO(func, ...) \
    do { if (LOG_INFO_ENABLED) { \
        fprintf(stdout, "[INFO] %s: ", func); \
        fprintf(stdout, __VA_ARGS__); \
        fprintf(stdout, "\n"); \
    } } while (0)

// Print log with debug level
#define LOG_DEBUG(func, ...) \
    do { if (LOG_DEBUG_ENABLED) { \
        fprintf(stdout, "[DEBUG] %s: ", func); \
        fprintf(stdout, __VA_ARGS__); \
        fprintf(stdout, "\n"); \
    } } while (0)

// Print log with detail level
#define LOG_DETAIL(func, ...) \
do { if (LOG_DETAIL_ENABLED) { \
    fprintf(stdout, "[DETAIL] %s: ", func); \
    fprintf(stdout, __VA_ARGS__); \
    fprintf(stdout, "\n"); \
} } while (0)

static inline void log_timestamp(const char* event_name) {
    if(LOG_DEBUG_ENABLED){
        struct timeval tv;
        gettimeofday(&tv, NULL);

        struct tm tm_storage; // on each thread's stack, allocate a private space

        // use thread-safe localtime_r function,
        // it will write the result to the provided tm_storage, instead of the global shared "danger zone"
        localtime_r(&tv.tv_sec, &tm_storage);

        char time_buffer[64];
        // now the data in tm_storage is completely safe, and will not be destroyed by other threads
        strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &tm_storage);
    
        fprintf(stderr, "[TIMESTAMP] %s: %s.%06ld\n", event_name, time_buffer, tv.tv_usec);
    }
}


#endif /* LOG_H */