#ifndef SHARE_SRV_H
#define SHARE_SRV_H

#include <stdio.h>
#include <stdlib.h>
#include <ev.h>
#include <shm_sock.h>
// INSERT_YOUR_CODE
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


#endif  // SHARE_SRV_H

