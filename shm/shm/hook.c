#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <stdarg.h>
#include <sys/un.h>
#include <netinet/in.h>
#include "shm_sock.h"
#include <time.h>

int sd = -1;

int cnt = 0;

struct uds_sock{
    int fd;
    struct sockaddr_un *addr;
    struct sockaddr_in *addr_in;
};

struct uds_sock *uds_sock_t = NULL;

ssize_t *noti = NULL;

ssize_t *rvlen = NULL;

#define SOCK_PATH "/tmp/openvpn_signal.sock"

typedef struct {
      char server_ip[16];
      uint16_t port;
      int packet_length;
      int test_duration;
      int conn_num;
} config_t;

int read_ger_config(const char *filename, config_t *config) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Failed to open config file");
        return -1;
    }

    // 解析配置文件
    fscanf(file, "server_ip=%15s\n", config->server_ip);
    fscanf(file, "port=%hu\n", &config->port);
    fscanf(file, "packet_length=%d\n", &config->packet_length);
    fscanf(file, "test_duration=%d\n", &config->test_duration);
    fscanf(file, "conn_num=%d\n", &config->conn_num);

    fclose(file);
    return 0;
}

/*int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    static int (*real_setsockopt)(int, int, int, const void *, socklen_t) = NULL;
    if (!real_setsockopt) {
        real_setsockopt = dlsym(RTLD_NEXT, "setsockopt");
    }

    if (uds_sock_t && sockfd == uds_sock_t->fd) {
        int on = 1;
        return real_setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    } else {
        return real_setsockopt(sockfd, level, optname, optval, optlen);
    }
}*/

int socket(int domain, int type, int protocol) {
    const char *config_file = NULL;

    static int (*real_socket)(int, int, int) = NULL;
    if (!real_socket) {
        real_socket = dlsym(RTLD_NEXT, "socket");
    }

    int init_domain = -1;

    if(type == SOCK_RAW || type == SOCK_DGRAM){
        return real_socket(domain, type, protocol);
    }else if(protocol == 1){
        config_file = "/home/emma/shm/cli.conf";
        init_domain = 1;
    }else if(protocol == 0){
        config_file = "/home/emma/shm/srv.conf";
        init_domain = 0;
    }

    uds_sock_t = (struct uds_sock *)malloc(sizeof(struct uds_sock));
    if(uds_sock_t == NULL){
        return -1;
    }
    uds_sock_t->addr_in = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    if(uds_sock_t->addr_in == NULL){
        perror("Failed to allocate memory for UDS socket address");
        return -1;
    }
    memset(uds_sock_t->addr_in, 0, sizeof(struct sockaddr_in));
    uds_sock_t->addr_in->sin_family = AF_INET;
    uds_sock_t->addr_in->sin_port = htons(1194);
    uds_sock_t->addr_in->sin_addr.s_addr = inet_addr("127.0.0.1");
    uds_sock_t->fd = real_socket(domain, type, 6);
    if (uds_sock_t->fd == -1) {
        perror("Failed to create UDS socket");
    }

    config_t *config = (config_t *)malloc(sizeof(config_t));
    if (read_ger_config("/home/emma/shm/config.txt", config) != 0) {
        return -1;
    }

    shm_config_t *shm_config = read_config(config_file);

    shm_init_global(*shm_config);

    sd = shm_socket(init_domain, shm_config->shm_size, protocol);

    if(!noti){
        noti = malloc(sizeof(ssize_t));
    }

    if(!rvlen){
        rvlen = malloc(sizeof(ssize_t));
    }

    return uds_sock_t->fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    static int (*real_connect)(int, const struct sockaddr *, socklen_t) = NULL;
    if (!real_connect) {
        real_connect = dlsym(RTLD_NEXT, "connect");
    }
    int status = -1; 

    if(uds_sock_t != NULL && sockfd == uds_sock_t->fd){
        status = real_connect(sockfd, (struct sockaddr*)uds_sock_t->addr_in, sizeof(struct sockaddr_in));
        if(status != 0){
            perror("Hook: UDS connect failed!");
        }

        usleep(100000);

        int ret = shm_connect(sd, (const char*)addr, addrlen);
        if (ret != 0) {
            perror("Hook: shm_connect failed!");
        }
    }else{
        status = real_connect(sockfd, addr, addrlen);
        if(status != 0){
            perror("Hook: connect failed!");
        }
    }
    return status;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    static int (*real_bind)(int, const struct sockaddr *, socklen_t) = NULL;
    if (!real_bind) {
        real_bind = dlsym(RTLD_NEXT, "bind");
    }
    int ret = -1;
    
    if(uds_sock_t != NULL && sockfd == uds_sock_t->fd){
        ret = real_bind(sockfd, (struct sockaddr*)uds_sock_t->addr_in, sizeof(struct sockaddr_in));
        if (ret != 0) {
            perror("Hook: UDS bind failed!");
        }

        int ret = shm_bind(sd, (const char*)addr, addrlen);
        if (ret != 0) {
            perror("Hook: shm_bind failed!");
        }
    }else{
        ret = real_bind(sockfd, addr, addrlen);
        if (ret != 0) {
            perror("Hook: bind failed!");
        }
    }
    return ret;
}


int listen(int sockfd, int backlog) {
    static int (*real_listen)(int, int) = NULL;
    if (!real_listen) {
        real_listen = dlsym(RTLD_NEXT, "listen");
    }
    int status = -1;

    if(uds_sock_t != NULL && sockfd == uds_sock_t->fd){
        status = real_listen(sockfd, backlog);
        if (status != 0) {
            perror("Hook: UDS listen failed!");
        }
    }else{
        status = real_listen(sockfd, backlog);
        if (status != 0) {
            perror("Hook: listen failed!");
        }
    }

    return status;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    static int (*real_accept)(int, struct sockaddr *, socklen_t *) = NULL;
    if (!real_accept) {
        real_accept = dlsym(RTLD_NEXT, "accept");
    }

    int ret = -1;

    if(uds_sock_t != NULL && sockfd == uds_sock_t->fd){
        
        ret = real_accept(sockfd, (struct sockaddr*)addr, addrlen);
        
        if(ret < 0){
            perror("Hook: accept failed!");
        }

        uds_sock_t->fd = ret;

        int status = shm_listen(sd, 15);
        if (status != 0) {
            perror("Hook: shm_listen failed!");
        }

        sd = shm_accept(sd, addr, addrlen);
        if(sd < 0){
            perror("Hook: shm_accept failed!");
        }

        LOG_DEBUG("Hook_accept", "Hook: accept success at socket %d!", ret);
    }else{
        ret = real_accept(sockfd, addr, addrlen);
        if(ret < 0){
            perror("Hook: accept failed!");
        }
    }
    return ret;
}


ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    static ssize_t (*real_send)(int, const void *, size_t, int) = NULL;
    if (!real_send) {
        real_send = dlsym(RTLD_NEXT, "send");
    }

    ssize_t ret = -1;

    if(uds_sock_t != NULL && sockfd == uds_sock_t->fd){
        ret = shm_write(sd, buf, len);  
        if (ret < 0) {
            perror("Hook: shm_write failed!");
            return ret;
        }else{
            LOG_DEBUG("Hook_send", "Hook: shm_write %ld bytes success!", ret);
        }
        
        *noti = ret;
        if(write(sockfd, noti, sizeof(ssize_t)) == -1) {
            perror("Hook: write Fifo failed!");
        }else{
            LOG_DEBUG("Hook_send", "Hook: write UDS notify bytes success!");
        }
    }else{
        ret = real_send(sockfd, buf, len, flags);
        if(ret < 0){
            perror("Hook: send failed!");
        }
    }
    return ret;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    static ssize_t (*real_recv)(int, void *, size_t, int) = NULL;
    if (!real_recv) {
        real_recv = dlsym(RTLD_NEXT, "recv");
    }
    ssize_t ret = 0;
    if(uds_sock_t != NULL && sockfd == uds_sock_t->fd){
        if(read(sockfd, rvlen, sizeof(ssize_t)) == -1){
            perror("Hook: read Fifo failed!");
        }else{
            LOG_DEBUG("Hook_recv", "Hook: read UDS notify bytes success!");
        }
        len = *rvlen;
        if(len == 0){
            errno = EAGAIN;
            return -1;
        }
        ret = shm_read(sd, buf, len);
        if(ret < 0){
            perror("Hook: shm_read failed!");
        }else if(ret == 0){
            LOG_INFO("Hook_recv", "Hook: shm_read 0 bytes!, require %zu bytes", len);
        }else{
            LOG_DEBUG("Hook_recv", "Hook: shm_read %ld bytes success!", ret);
            *rvlen = 0;
        }
        /*struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        while(ret == 0){
            ret = shm_read(sd, buf, len);
            if(ret < 0){
                perror("Hook: shm_read failed!");
            }else if(ret > 0){
                LOG_INFO("Hook_recv", "Hook: shm_read %ld bytes success!", ret);
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
            if(elapsed > 2.0){
                // 超时2秒
                break;
            }
        }*/
    }else{
        ret = real_recv(sockfd, buf, len, flags);
        if(ret < 0){
            perror("Hook: recv failed!");
        }
    }
    return ret;
}

/*int close(int fd) {
    static int (*real_close)(int) = NULL;
    if (!real_close) {
        real_close = dlsym(RTLD_NEXT, "close");
    }
    int ret = -1;

    ret = real_close(fd);

    if(uds_sock_t != NULL && fd == uds_sock_t->fd){
        shm_close(sd);
    }
    return ret;
}*/
