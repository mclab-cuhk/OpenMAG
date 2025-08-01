#ifndef SHM_SOCK_H
#define SHM_SOCK_H

#include <sys/socket.h>
#include <sys/types.h>
#include <stdbool.h>

// Conditional includes for atomic types
#ifdef __cplusplus
#include <atomic> // C++ atomic header
#else
#include <stdatomic.h> 
#include <stdalign.h>// C11 atomic header
#endif

#include "shm_debug.h"

#define MAX_CONNECTIONS 1024

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        SHM_MODE_CLIENT,
        SHM_MODE_SERVER
    } shm_mode_t;

    typedef struct {
        int max_conn;
        shm_mode_t mode;
        size_t shm_size;
    } shm_config_t;

    typedef struct {
        
    #ifdef __cplusplus
        std::atomic<void *> ptr;
        std::atomic<size_t> capacity;
        std::atomic<size_t> read_offset;
        std::atomic<size_t> write_offset;
    #else
        _Atomic(void *) ptr;
        atomic_size_t capacity;
        atomic_size_t read_offset;
        atomic_size_t write_offset;
    #endif
    } shm_buffer_t;

    typedef struct {
        int fd;
        bool is_passive;
        char name[32];
    #ifdef __cplusplus
        std::atomic<int> status;
    #else
        atomic_int status;
    #endif
        shm_buffer_t buffer;
    } shm_socket_t;

    typedef struct {
        int fd;
    #ifdef __cplusplus
        std::atomic<shm_socket_t *> socket;
    #else
        _Atomic(shm_socket_t *) socket;
    #endif
    } shm_connection_t;

    typedef struct {
        shm_connection_t write_entries[MAX_CONNECTIONS];
        shm_connection_t read_entries[MAX_CONNECTIONS];
        int count;
    } shm_connection_table_t;

    typedef struct {
        shm_connection_table_t connections;
        int total_conn;
    } shm_global_t;


    void *alloc_shm(const char *name, size_t size);
    void *read_shm(const char *name);

    // API declartions
    int shm_socket(int domain, int type, int protocol);
    int shm_connect(int fd, const char *addr, socklen_t addrlen);
    int shm_bind(int sockfd, const char *addr, socklen_t addrlen);
    int shm_listen(int sockfd, int backlog);
    int shm_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    ssize_t shm_read(int fd, void *buf, size_t count);
    ssize_t shm_write(int fd, const void *buf, size_t count);
    int shm_close(int fd);

    shm_config_t *read_config(const char *config_file);
    void cleanup_shared_memory(int signum);
    void shm_init_global(shm_config_t config);

#ifdef __cplusplus
}
#endif

#endif // SHM_SOCK_H
