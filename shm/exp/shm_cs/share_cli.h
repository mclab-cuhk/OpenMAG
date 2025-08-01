#ifndef SHARE_CLI_H
#define SHARE_CLI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ev.h>
#include <shm_sock.h>

#define SHM_NAME "/my_shared_memory" // 共享内存名称
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

typedef struct {
    char server_ip[16];
    int port;
    int packet_length;
    int test_duration;
    int conn_num;
} config_t;

// 客户端信息结构体
typedef struct {
    int client_id;
    int sock;
    int shm_sock;
    config_t config;
    shm_config_t shm_config;
    char send_buff[BUFFER_SIZE];
    char recv_buff[BUFFER_SIZE];
} client_info_t;


int read_cli_config(const char *filename, config_t *config);
void send_to_remote_cb(struct ev_loop *loop, ev_io *watcher, int revents);
void write_response_cb(struct ev_loop *loop, ev_io *watcher, int revents);
void *shm_server(void *arg);
void *recv_remote(void *arg);
int tcp_client(client_info_t *client_info);

#endif

