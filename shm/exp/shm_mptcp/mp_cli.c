#include <linux/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <semaphore.h>

#include "mp_cli.h" // 

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

int request_counts[MAX_CLIENTS] = {0}; // 全局变量计数器数组
int response_counts[MAX_CLIENTS] = {0};
size_t response_bytes[MAX_CLIENTS] = {0};

sem_t *sem_r = NULL;

sem_t *sem_w = NULL;

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
    if (send(client_info->sock, client_info->recv_buff, strlen(client_info->recv_buff), 0) < 0) {
        LOG_ERROR(__func__, "Send failed");
        ev_io_stop(loop, watcher); // 停止监听事件
        int sock = client_info->sock;
        client_info->sock = -2;
        close(sock);
        return;
    }
    LOG_DEBUG(__func__, "Sent to remote server: %s\n", client_info->recv_buff);
    request_counts[client_info->client_id]++;

    // 停止监听事件
    ev_io_stop(loop, watcher);
}

void *shm_server(void *arg){
    client_info_t *client_info = (client_info_t *)arg;
    int cid = client_info->client_id;
    config_t config = client_info->config;
    shm_config_t shm_config = client_info->shm_config;

    struct ev_loop *loop = ev_default_loop(0);
    ev_io socket_watcher;
    int listen_fd = -1;
    // 事件回调初始化
    ev_io_init(&socket_watcher, send_to_remote_cb, client_info->sock, EV_WRITE);
    socket_watcher.data = arg;

    int conn_fds[3];
    int i = 0;
   
    int server_fd = -1;
    struct sockaddr_in address;

    // 1. 创建 shm socket
    if ((server_fd = shm_socket(0, shm_config.shm_size, 0)) < 0) {
        LOG_ERROR(__func__, "socket failed");
        cleanup_shared_memory(1);
        exit(EXIT_FAILURE);
    }

    // 2. 绑定地址和端口
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config.port);

    if (shm_bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR(__func__, "bind failed");
        cleanup_shared_memory(1);
        exit(EXIT_FAILURE);
    }

    // 3. 监听连接
    if (shm_listen(server_fd, 10) < 0) {
        LOG_ERROR(__func__, "listen failed");
        cleanup_shared_memory(1);
        exit(EXIT_FAILURE);
    }

    int sock = shm_accept(server_fd, (struct sockaddr *)&address, sizeof(address));

    client_info->shm_sock = sock;
    
    while(client_info->sock > -2) {
        // 等待客户端通知
        sem_wait(sem_w);  // 信号量 -1，等待信号

        ssize_t bytes_read = shm_read(sock, client_info->recv_buff, sizeof(client_info->recv_buff));
        if (bytes_read < 0) {
            LOG_ERROR(__func__, "Failed to read from socket");
            break;
        }
        client_info->recv_buff[bytes_read] = '\0'; // 确保字符串结束
        LOG_DEBUG(__func__, "Read from shared memory: %s\n", client_info->recv_buff);

        // 启动 libev 事件，调用回调函数处理数据发送
        /*ev_io_start(loop, &socket_watcher);

        // 等待数据处理完毕
        ev_run(loop, 0);*/
       if (send(client_info->sock, client_info->recv_buff, strlen(client_info->recv_buff), 0) < 0) {
            LOG_ERROR(__func__, "Send failed");
            int sock = client_info->sock;
            client_info->sock = -2;
            close(sock);
            return;
        }
        LOG_DEBUG(__func__, "Sent to remote server: %s\n", client_info->recv_buff);
        request_counts[client_info->client_id]++;
    }

    // 关闭监听socket
    if (close(client_info->sock) == -1) {
        LOG_ERROR(__func__, "Failed to close listening socket\n");
        cleanup_shared_memory(1);
    }
    LOG_INFO(__func__, "Closed listening socket %d\n", listen_fd);

    sem_destroy(sem_w);
}

void write_response_cb(struct ev_loop *loop, ev_io *watcher, int revents) {
    if (EV_ERROR & revents) {
        LOG_ERROR(__func__, "Invalid event");
        return;
    }

    client_info_t *client_info = (client_info_t *)watcher->data;
    int sock = client_info->shm_sock;

    if (shm_write(sock, client_info->send_buff, strlen(client_info->send_buff)) < 0) {
        LOG_ERROR(__func__, "Send failed");
        ev_io_stop(loop, watcher); // 停止监听事件
        shm_close(sock);
        return;
    }
    sem_post(sem_r); 
    LOG_DEBUG(__func__, "Sent response to Client: %s\n", client_info->send_buff);

    // 停止监听事件
    ev_io_stop(loop, watcher);
}

void *recv_remote(void *arg){
    client_info_t *client_info = (client_info_t *)arg;
    int cid = client_info->client_id;
    int sock = client_info->sock;
    config_t config = client_info->config;
    shm_config_t shm_config = client_info->shm_config;
    struct ev_loop *loop = ev_default_loop(0);
    ev_io socket_watcher;

    struct sockaddr_in server_addr;

    /*ev_io_init(&socket_watcher, write_response_cb, client_info->sock, EV_WRITE);
    socket_watcher.data = arg;*/

    while (client_info->sock > -2) {
        // 接收响应并计算往返时间
        ssize_t bytes_received = recv(sock, client_info->send_buff, config.packet_length, 0);
        if (bytes_received < 0) {
            continue;
        }
        client_info->send_buff[bytes_received] = '\0';
        LOG_DEBUG(__func__, "Recv response from remote Server: %s\n", client_info->send_buff);
        response_counts[cid]++; // 更新计数器
        response_bytes[cid] += bytes_received;

        // 启动 libev 事件，调用回调函数处理数据发送
        /*ev_io_start(loop, &socket_watcher);

        // 等待数据处理完毕
        ev_run(loop, 0);*/
        ssize_t send_bytes = -1;
        while (send_bytes == -1 || send_bytes == -2) {
            send_bytes = shm_write(client_info->shm_sock, client_info->send_buff, strlen(client_info->send_buff));
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
                LOG_DEBUG(__func__, "Sent response to Client: %s\n", client_info->send_buff);
            }
        }
        
    }
    pthread_exit(NULL);
}

int tcp_client(client_info_t *client_info){
    // 创建TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);
    if (sock < 0) {
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
        close(sock);
        return -1;
    }

    // 连接到服务器
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }
    LOG_INFO(__func__, "Connected to server\n");

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

    // Initialize global shared memory
    shm_init_global(shm_config);

    if (read_cli_config("config.txt", &config) != 0) {
        return -1;
    }

    pthread_t shm_thread[MAX_CLIENTS], tcp_thread[MAX_CLIENTS];
    client_info_t client_info[MAX_CLIENTS];

     // 映射共享内存
    void *ptr = alloc_shm("sem_r", sizeof(sem_t));
    if(ptr == NULL){
        LOG_ERROR(__func__, "Alloc sem_r shm failed.");
        exit(EXIT_FAILURE);
    }

    sem_r = (sem_t *)ptr;

    // 初始化信号量（进程间共享，初始值为0）
    if (sem_init(sem_r, 1, 0) == -1) {
        perror("sem_r init failed");
        exit(EXIT_FAILURE);
    }

    void *wptr = alloc_shm("sem_w", sizeof(sem_t));
    if(wptr == NULL){
        perror("Alloc sem_w shm failed.");
        exit(EXIT_FAILURE);
    }

    sem_w = (sem_t *)wptr;

    // 初始化信号量（进程间共享，初始值为0）
    if (sem_init(sem_w, 1, 0) == -1) {
        perror("sem_w init failed");
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < config.conn_num; i++){
        client_info[i].sock = -1;
        client_info[i].client_id = i;
        client_info[i].config = config;
        client_info[i].shm_config = shm_config;
        if(tcp_client(&client_info[i]) < 0){
            LOG_ERROR(__func__, "Connect to remote server failed");
            return -1;
        }
        if(pthread_create(&shm_thread[i], NULL, shm_server, &client_info[i]) != 0){
            perror("Failed to create shm_server thread!\n");
        }
        if(pthread_create(&tcp_thread[i], NULL, recv_remote, &client_info[i]) != 0){
            perror("Failed to create tcp_client thread!\n");
        }
    }

    for(int i = 0; i < config.conn_num; i++){
        pthread_join(shm_thread[i], NULL);
        pthread_join(tcp_thread[i], NULL);
    }
    return 0;
}