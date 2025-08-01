#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ev.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#include <shm_sock.h>

#define REQUEST_COUNT 100

int tc = -1;

struct client_watcher {
    struct ev_io io_watcher;
    struct ev_timer timer_watcher;
    int connection_idx; // Index for fds, srv_to_cli_pipefds, etc.
    int shm_fd;         // The shared memory socket descriptor for this connection
    int usd_fd;         // The UDS socket descriptor for this connection
    int response_count; // To track responses for this connection
    int request_count; // To track requests for this connection
};

#define UDS_PATH "/tmp/app_srv.sock"

void handle_error(const char *message) {
    fprintf(stderr, "%s\n", message);
    cleanup_shared_memory(1);
}

void handle_read(struct ev_loop *loop, struct ev_io *w, int revents) {
    
    // Handle read event
    struct client_watcher *watcher = (struct client_watcher *)w->data;
    ssize_t *dummy = (ssize_t *)malloc(sizeof(ssize_t));
    if(EV_ERROR & revents) {
        handle_error("Error event");
    }

    ssize_t nread = 0;
    nread = read(watcher->usd_fd, dummy, sizeof(ssize_t));
    if(nread < 0) {
        handle_error("Failed to read from pipe");
    }else if(nread == 0) {
        handle_error("Pipe closed");
    }

    char response[256];
    size_t count = *dummy;
    int ret = shm_read(watcher->shm_fd, response, count);
    if (ret == -1) {
        handle_error("Failed to receive response");
    }else if(ret == 0){
        handle_error("SHM closed");
    }

    printf("Received response: %s\n", response);
    watcher->response_count++;
    free(dummy);
    
}

void handle_write(struct ev_loop *loop, struct ev_timer *w, int revents) {
    struct client_watcher *watcher = (struct client_watcher *)w->data;
    if(EV_ERROR & revents) {
        handle_error("Error event");
    }
    
    if(watcher->request_count == REQUEST_COUNT) {
        ev_timer_stop(loop, w);
        return;
    }

    char request[256];
    snprintf(request, sizeof(request), "Request %d from connection %d", watcher->request_count + 1, watcher->connection_idx + 1);
    printf("Sending request %d from connection %d:%s\n", watcher->request_count + 1, watcher->connection_idx + 1, request);
    ssize_t ret = shm_write(watcher->shm_fd, request, strlen(request));
    if (ret == -1) {
        handle_error("Failed to send request");
    }
    
    ssize_t *ret_ptr = (ssize_t *)malloc(sizeof(ssize_t));
    *ret_ptr = ret;
    if(write(watcher->usd_fd, ret_ptr, sizeof(ssize_t)) == -1) {
        handle_error("Failed to send notification");
    }
    free(ret_ptr);
    watcher->request_count++;
    
}

int main(int argc, char *argv[]) {

    // 从命令行参数读取配置文件
    if (argc < 3 || strcmp(argv[1], "-c") != 0) {
        fprintf(stderr, "Usage: %s -c <config_file>\n", argv[0]);
        exit(1);
    }
    const char *config_file = argv[2];

    // Read configuration file
    shm_config_t *config = read_config(config_file);

    // Initialize global shared memory
    shm_init_global(*config);

    // Establish three connections
    int fds[3];

    struct client_watcher watchers[3];

    struct ev_loop *loop = EV_DEFAULT;

    // Create and connect ONE UDS socket for all connections
    int uds_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (uds_fd == -1) {
        handle_error("Failed to create UDS socket");
    }

    struct sockaddr_un uds_addr;
    memset(&uds_addr, 0, sizeof(uds_addr));
    uds_addr.sun_family = AF_UNIX;
    strncpy(uds_addr.sun_path, UDS_PATH, sizeof(uds_addr.sun_path) - 1);

    if (connect(uds_fd, (struct sockaddr*)&uds_addr, sizeof(uds_addr)) == -1) {
        handle_error("Failed to connect UDS socket");
    }

    usleep(100000);

    for (int i = 0; i < 1; i++) {
        // Create SHM socket
        fds[i] = shm_socket(i + 1, config->shm_size, 0);
        
        if (fds[i] == -1) {
            handle_error("Failed to create socket");
        }

        if (shm_connect(fds[i], "server_address", strlen("server_address")) == -1) {
            handle_error("Failed to connect socket");
        }

        
        watchers[i].connection_idx = i;
        watchers[i].shm_fd = fds[i];
        watchers[i].request_count = 0;
        watchers[i].response_count = 0;
        watchers[i].usd_fd = uds_fd;

        ev_io_init(&watchers[i].io_watcher, handle_read, watchers[i].usd_fd, EV_READ);
        watchers[i].io_watcher.data = &watchers[i];
        ev_io_start(loop, &watchers[i].io_watcher);

        ev_timer_init(&watchers[i].timer_watcher, handle_write, 0.01, 0.5);
        watchers[i].timer_watcher.data = &watchers[i];
        ev_timer_start(loop, &watchers[i].timer_watcher);

        printf("Connected SHM socket %d and UDS socket %d\n", fds[i], uds_fd);
    }

    // 映射共享内存
    /*void *ptr = mmap(0, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_sem_fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    sem_t *sem = (sem_t*)ptr;
    sem_t *sem_server_to_client = (sem_t *)((char*)ptr + sizeof(sem_t));*/

    

    // Send 10 requests and receive 10 responses for each connection

    ev_run(loop, 0);

    // Close connections
    for (int i = 0; i < 1; i++) {
        ev_timer_stop(loop, &watchers[i].timer_watcher);
        ev_io_stop(loop, &watchers[i].io_watcher);
        if (shm_close(fds[i]) == -1) {
            handle_error("Failed to close socket");
        }
    }
    close(uds_fd);

    //sem_destroy(sem);

    return 0;
}
