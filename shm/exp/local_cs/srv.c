#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ev.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <shm_sock.h>

#define UDS_PATH "/tmp/app_srv.sock"

struct client_watcher {
    struct ev_io io_watcher;
    int client_idx;     // Index for conn_fds, pipe arrays, etc.
    int shm_conn_fd;    // The shm_accept-ed fd for this client
    int usd_fd;         // The usd_socket for this client
    int request_count;  // To track requests from this client
};

void handle_error(const char *message) {
    fprintf(stderr, "%s\n", message);
    cleanup_shared_memory(1);
    exit(EXIT_FAILURE);
}

void handle_read(struct ev_loop *loop, struct ev_io *w, int revents) {
    struct client_watcher *watcher = (struct client_watcher *)w->data;
    ssize_t *dummy = (ssize_t *)malloc(sizeof(ssize_t));

    if (revents & EV_ERROR) {
        handle_error("EV_ERROR in handle_read");
        return;
    }

    // Read notification from UDS
    ssize_t nread = read(watcher->usd_fd, dummy, sizeof(ssize_t));
    if (nread < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; // Should not happen for level-triggered
        }
        perror("UDS read error");
        ev_io_stop(loop, w);
        close(watcher->usd_fd);
        return;
    }

    if (nread == 0) {
        printf("Client %d of UDS %d disconnected.\n", watcher->client_idx, watcher->usd_fd);
        ev_io_stop(loop, w);
        close(watcher->usd_fd);
        close(watcher->shm_conn_fd);
        // In a real app, you might free the watcher if dynamically allocated
        return;
    }

    char request[256];
    size_t count = *dummy;
    ssize_t ret = shm_read(watcher->shm_conn_fd, request, count);
    if (ret == -1) {
        handle_error("Failed to receive request");
    }else if(ret == 0){
        printf("Client SHM disconnected at request %d.\n", watcher->client_idx, watcher->request_count);
        ev_io_stop(loop, w);
        close(watcher->usd_fd);
        shm_close(watcher->shm_conn_fd);
        // In a real app, you might free the watcher if dynamically allocated
        return;
    }else{
        printf("Received request: %s\n", request);
    }

    free(dummy);

    char response[256];
    snprintf(response, sizeof(response), "Response %d from Connection %d", watcher->request_count, watcher->client_idx);
    ret = shm_write(watcher->shm_conn_fd, response, strlen(response) + 1);
    if (ret < 0) {
        handle_error("Failed to send response to SHM");
    } else {
        printf("Sent response %d to client: %s\n", watcher->request_count, response);
        // Send notification back via UDS
        ssize_t *ret_ptr = (ssize_t *)malloc(sizeof(ssize_t));
        *ret_ptr = ret;
        if(write(watcher->usd_fd, ret_ptr, sizeof(ssize_t)) < 0) {
            perror("Failed to send notification via UDS");
        }
        free(ret_ptr);
    }
    watcher->request_count++;
}

int main(int argc, char *argv[]) {
    if (argc < 3 || strcmp(argv[1], "-c") != 0) {
        fprintf(stderr, "Usage: %s -c <config_file>\n", argv[0]);
        exit(1);
    }
    const char *config_file = argv[2];
    shm_config_t *config = read_config(config_file);

    struct ev_loop *loop = EV_DEFAULT;

    // Initialize global shared memory
    shm_init_global(*config);

    // Create a UDS listening socket
    int uds_listen_fd;
    struct sockaddr_un uds_addr;
    if ((uds_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        handle_error("UDS socket error");
    }
    
    memset(&uds_addr, 0, sizeof(uds_addr));
    uds_addr.sun_family = AF_UNIX;
    strncpy(uds_addr.sun_path, UDS_PATH, sizeof(uds_addr.sun_path) - 1);
    unlink(UDS_PATH);

    if (bind(uds_listen_fd, (struct sockaddr*)&uds_addr, sizeof(uds_addr)) == -1) {
        handle_error("UDS bind error");
    }
    if (listen(uds_listen_fd, 15) == -1) {
        handle_error("UDS listen error");
    }
    printf("UDS server listening on %s\n", UDS_PATH);

    // Accept UDS connection
    int uds_conn_fd = accept(uds_listen_fd, NULL, NULL);
    if (uds_conn_fd < 0) {
        handle_error("Accept UDS connection failed");
    }
    printf("Accepted UDS connection %d for client\n", uds_conn_fd);

    int listen_fd = shm_socket(0, config->shm_size, 0);
    if (listen_fd == -1) {
        fprintf(stderr, "Create listening socket failed\n");
        cleanup_shared_memory(1);
    }

    if (shm_bind(listen_fd, "server_address", strlen("server_address")) < 0) {
        handle_error("SHM bind error");
    }

    if (shm_listen(listen_fd, 15) == -1) {
        fprintf(stderr, "Listen socket failed\n");
        cleanup_shared_memory(1);
    }
    printf("Listening socket %d is now listening\n", listen_fd);

    
    int conn_fds[3];
    int i = 0;

    static struct client_watcher client_watchers[3];
    // 接受连接并处理请求
    for(i = 0; i < 1; i++) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        conn_fds[i] = shm_accept(listen_fd, &addr, &addrlen);
        if (conn_fds[i] == -1) {
        fprintf(stderr, "Accept connection %d failed\n", conn_fds[i]);
            cleanup_shared_memory(1);
        }
        printf("Accepted SHM connection %d for client %d\n", conn_fds[i], i);

        // Initialize watcher for this client
        client_watchers[i].client_idx = i;
        client_watchers[i].shm_conn_fd = conn_fds[i];
        client_watchers[i].usd_fd = uds_conn_fd;
        client_watchers[i].request_count = 0;

        // Set up the IO watcher for the UDS socket
        ev_io_init(&client_watchers[i].io_watcher, handle_read, client_watchers[i].usd_fd, EV_READ);
        client_watchers[i].io_watcher.data = &client_watchers[i];
        ev_io_start(loop, &client_watchers[i].io_watcher);
    }

    printf("Server setup complete. Running event loop.\n");
    ev_run(loop, 0);

    // Cleanup
    for(i = 0; i < 1; i++) {
        shm_close(conn_fds[i]);
    }

    close(uds_listen_fd);
    unlink(UDS_PATH);
    
    return 0;
}