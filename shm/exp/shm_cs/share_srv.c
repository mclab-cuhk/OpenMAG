#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <semaphore.h>

#include "share_srv.h" // 

int request_counts[MAX_CLIENTS] = {0}; // 全局变量计数器数组
int response_counts[MAX_CLIENTS] = {0};
size_t response_bytes[MAX_CLIENTS] = {0};

sem_t *sem_r;

sem_t *sem_w;

// 从配置文件读取配置信息
int read_cli_config(const char *filename, config_t *config) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        LOG_ERROR(__func__, "Failed to open config file");
        return -1;
    }

    // 解析配置文件
    fscanf(file, "server_ip=%15s\n", config->server_ip);
    fscanf(file, "port=%d\n", &config->port);
    fscanf(file, "packet_length=%d\n", &config->packet_length);
    fscanf(file, "test_duration=%d\n", &config->test_duration);
    fscanf(file, "conn_num=%d\n", &config->conn_num);

    fclose(file);
    return 0;
}

void send_to_remote_cb(struct ev_loop *loop, ev_io *watcher, int revents) {
    if (EV_ERROR & revents) {
        LOG_ERROR(__func__, "Invalid event");
        return;
    }

    client_info_t *client_info = (client_info_t *)watcher->data;
    
    // 发送数据到远端服务器
    if (send(client_info->sock, client_info->send_buff, strlen(client_info->send_buff), 0) < 0) {
        LOG_ERROR(__func__, "Send failed");
        ev_io_stop(loop, watcher); // 停止监听事件
        int sock = client_info->sock;
        client_info->sock = -2;
        close(sock);
        return;
    }
    LOG_DEBUG(__func__, "Sent to remote client: %s\n", client_info->send_buff);
    request_counts[client_info->client_id]++;

    // 停止监听事件
    ev_io_stop(loop, watcher);
}

void write_request_cb(struct ev_loop *loop, ev_io *watcher, int revents) {
    if (EV_ERROR & revents) {
        LOG_ERROR(__func__, "Invalid event");
        return;
    }

    client_info_t *client_info = (client_info_t *)watcher->data;
    int shm_sock = client_info->shm_sock;

    // 发送数据到远端服务器
    if (shm_write(shm_sock, client_info->recv_buff, strlen(client_info->recv_buff)) < 0) {
        LOG_ERROR(__func__, "Send failed");
        ev_io_stop(loop, watcher); // 停止监听事件
        shm_close(shm_sock);
        return;
    }
    sem_post(sem_r); 
    LOG_DEBUG(__func__, "Sent to server: %s\n", client_info->recv_buff);
    request_counts[client_info->client_id]++;

    // 停止监听事件
    ev_io_stop(loop, watcher);
}

void *shm_client(void *arg){
    client_info_t *client_info = (client_info_t *)arg;
    int cid = client_info->client_id;
    config_t config = client_info->config;
    shm_config_t shm_config = client_info->shm_config;

    struct ev_loop *loop = ev_default_loop(0);
    ev_io socket_watcher;
    int listen_fd = -1;
    // 事件回调初始化
    /*ev_io_init(&socket_watcher, write_request_cb, client_info->sock, EV_WRITE);
    socket_watcher.data = arg;*/

    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    
    while(client_info->sock > -2) {
        // 收取远程客户端发送的tcp请求
        ssize_t bytes_received = recv(client_info->sock, client_info->recv_buff, sizeof(client_info->recv_buff), 0);
        if (bytes_received < 0) {
            continue;
        }
        client_info->recv_buff[bytes_received] = '\0'; // 确保字符串结束
        LOG_DEBUG(__func__, "Received from remote client: %s\n", client_info->recv_buff);

        // 触发回调函数
        /*ev_io_start(loop, &socket_watcher);
        // 等待数据处理完毕
        ev_run(loop, 0);*/
        ssize_t send_bytes = -1;
        while (send_bytes == -1 || send_bytes == -2) {
            send_bytes = shm_write(client_info->shm_sock, client_info->recv_buff, strlen(client_info->recv_buff));
            if (send_bytes < 0) {
                if (send_bytes == -1) {
                    LOG_ERROR(__func__, "Send failed");
                    break;  // 发送失败，退出循环
                } else if (send_bytes == -2) {
                    // 这里可以添加一些延时或其他逻辑，等待再次尝试发送
                    LOG_DEBUG(__func__, "Send temporarily failed, retrying...");
                }
            } else {
                sem_post(sem_r);  // 发送成功后，发布信号量
            }
        }
    }

    pthread_exit(NULL);
}

void *send_remote(void *arg){
    client_info_t *client_info = (client_info_t *)arg;
    int cid = client_info->client_id + 1;
    int sock = client_info->sock;
    config_t config = client_info->config;
    shm_config_t shm_config = client_info->shm_config;
    struct ev_loop *loop = ev_default_loop(0);
    ev_io socket_watcher;

    struct sockaddr_in server_addr;

    /*ev_io_init(&socket_watcher, send_to_remote_cb, client_info->sock, EV_WRITE);
    socket_watcher.data = arg;*/

    // 创建shm socket
    int shm_sock_id = shm_socket(cid, shm_config.shm_size, 0);
    if (shm_sock_id == -1) {
        LOG_ERROR(__func__, "创建shm socket失败");
        pthread_exit(NULL);
    }
    LOG_INFO(__func__, "成功创建shm socket %d\n", shm_sock_id);

    // 创建shm连接
    if (shm_connect(shm_sock_id, NULL, sizeof(server_addr)) == -1) {
        LOG_ERROR(__func__, "创建shm连接失败");
        pthread_exit(NULL);
    }
    LOG_INFO(__func__, "成功创建shm连接 %d\n", shm_sock_id);

    client_info->shm_sock = shm_sock_id;

    while (client_info->sock > -2) {
        sem_wait(sem_w);
        // 接收响应并计算往返时间
        ssize_t bytes_received = shm_read(shm_sock_id, client_info->send_buff, config.packet_length);
        if (bytes_received < 0) {
            continue;
        }
        client_info->send_buff[bytes_received] = '\0';

        /*ev_io_start(loop, &socket_watcher);

        ev_run(loop, 0);*/
        if (send(client_info->sock, client_info->send_buff, strlen(client_info->send_buff), 0) < 0) {
            LOG_ERROR(__func__, "Send failed");
            int sock = client_info->sock;
            client_info->sock = -2;
            close(sock);
            return;
        }
        LOG_DEBUG(__func__, "Sent to remote client: %s\n", client_info->send_buff);
        response_counts[client_info->client_id]++;
        response_bytes[cid] += bytes_received;
    }
    pthread_exit(NULL);
}

int tcp_server(client_info_t *client_info){
    // 创建TCP socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Socket creation error");
        return -1;
    }

    // 设置服务器地址
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(client_info->config.port);

    // 将服务器IP地址转换为二进制形式
    if (inet_pton(AF_INET, client_info->config.server_ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(listen_fd);
        return -1;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return -1;
    }

    // 监听连接
    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Bind failed");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 15) < 0) {
        perror("Listen failed");
        close(listen_fd);
        return -1;
    }
    LOG_INFO(__func__, "Listening for connections\n");

    socklen_t addr_len = sizeof(serv_addr);
    // 接受连接
    int sock = accept(listen_fd, (struct sockaddr *)&serv_addr, &addr_len);
    if (sock < 0) {
        perror("Accept failed");
        close(listen_fd);
        return -1;
    }
    LOG_INFO(__func__, "Accepted connection\n");

    // 更新客户端信息的socket
    client_info->sock = sock;
    return 0;
}

int main(int argc, char *argv[]) {
    config_t config;
    // 创建共享内存
    if (argc < 3 || strcmp(argv[1], "-c") != 0) {
        LOG_ERROR(__func__, "Usage: %s -c <config_file>\n", argv[0]);
        exit(1);
    }
    const char *config_file = argv[2];
    // Read configuration file
    shm_config_t shm_config = read_config(config_file);

    sem_r = NULL;
    sem_w = NULL;

    signal(SIGPIPE, SIG_IGN);

    // Initialize global shared memory
    shm_init_global(shm_config);

    if (read_cli_config("config.txt", &config) != 0) {
        return -1;
    }

    pthread_t shm_thread[MAX_CLIENTS], tcp_thread[MAX_CLIENTS];
    client_info_t client_info[MAX_CLIENTS];

    // 映射共享内存
    void *ptr = read_shm("sem_r");
    if(ptr == NULL){
        LOG_ERROR(__func__, "Read sem_r shm failed.");
        exit(EXIT_FAILURE);
    }

    sem_r = (sem_t *)ptr;

    void *wptr = read_shm("sem_w");
    if(wptr == NULL){
        LOG_ERROR(__func__, "Read sem_w shm failed.");
        exit(EXIT_FAILURE);
    }

    sem_w = (sem_t *)wptr;

    for(int i = 0; i < config.conn_num; i++){
        client_info[i].sock = -1;
        client_info[i].client_id = i;
        client_info[i].config = config;
        client_info[i].shm_config = shm_config;
        if(tcp_server(&client_info[i]) < 0){
            LOG_ERROR(__func__, "Connect to remote client failed");
            return -1;
        }
        if(pthread_create(&tcp_thread[i], NULL, send_remote, &client_info[i]) != 0){
            LOG_ERROR(__func__, "Failed to create tcp_client thread!\n");
        }
        if(pthread_create(&shm_thread[i], NULL, shm_client, &client_info[i]) != 0){
            LOG_ERROR(__func__, "Failed to create shm_server thread!\n");
        }
    }

    for(int i = 0; i < config.conn_num; i++){
        pthread_join(shm_thread[i], NULL);
        pthread_join(tcp_thread[i], NULL);
    }
    return 0;
}