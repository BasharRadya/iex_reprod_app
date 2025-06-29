#include "network_app.h"

void *server_thread_func(void *arg) {
    server_thread_meta_t *meta = (server_thread_meta_t *)arg;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct epoll_event event, events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    
    // Create listen socket
    meta->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (meta->listen_fd == -1) {
        count_socket_error(errno);
        perror("socket");
        return NULL;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(meta->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        count_socket_error(errno);
        perror("setsockopt");
        close(meta->listen_fd);
        return NULL;
    }
    
    // Set non-blocking
    if (set_socket_nonblocking(meta->listen_fd) == -1) {
        close(meta->listen_fd);
        return NULL;
    }
    
    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(g_ctx.listen_ip);
    server_addr.sin_port = htons(meta->port);
    
    if (bind(meta->listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        count_socket_error(errno);
        perror("bind");
        close(meta->listen_fd);
        return NULL;
    }
    
    // Listen
    if (listen(meta->listen_fd, 1) == -1) {
        count_socket_error(errno);
        perror("listen");
        close(meta->listen_fd);
        return NULL;
    }
    
    // Create epoll
    meta->epoll_fd = epoll_create1(0);
    if (meta->epoll_fd == -1) {
        count_socket_error(errno);
        perror("epoll_create1");
        close(meta->listen_fd);
        return NULL;
    }
    
    // Add listen socket to epoll
    event.events = EPOLLIN;
    event.data.fd = meta->listen_fd;
    if (epoll_ctl(meta->epoll_fd, EPOLL_CTL_ADD, meta->listen_fd, &event) == -1) {
        count_socket_error(errno);
        perror("epoll_ctl");
        close(meta->listen_fd);
        close(meta->epoll_fd);
        return NULL;
    }
    
    // Server runs silently - no startup messages
    
    while (g_ctx.running) {
        int nfds = epoll_wait(meta->epoll_fd, events, MAX_EVENTS, 100);
        if (nfds == -1) {
            if (errno != EINTR) {
                count_socket_error(errno);
                perror("epoll_wait");
            }
            continue;
        }
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == meta->listen_fd) {
                // New connection
                if (!meta->accepted_socket.is_active) {
                    int client_fd = accept(meta->listen_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd == -1) {
                        count_socket_error(errno);
                        perror("accept");
                        continue;
                    }
                    
                    if (set_socket_nonblocking(client_fd) == -1) {
                        close(client_fd);
                        continue;
                    }
                    
                    meta->accepted_socket.socket_fd = client_fd;
                    meta->accepted_socket.bytes_received = 0;
                    meta->accepted_socket.bytes_sent = 0;
                    meta->accepted_socket.is_active = 1;
                    meta->total_accepts++;
                    
                    // Add to epoll - only listen for incoming data initially
                    event.events = EPOLLIN;
                    event.data.fd = client_fd;
                    if (epoll_ctl(meta->epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                        count_socket_error(errno);
                        perror("epoll_ctl");
                        close(client_fd);
                        meta->accepted_socket.is_active = 0;
                    }
                }
            } else {
                // Client socket event
                int client_fd = events[i].data.fd;
                if (events[i].events & EPOLLIN) {
                    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
                    if (bytes_read <= 0) {
                        if (bytes_read == 0) {
                            // Connection closed cleanly
                            epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                            close(client_fd);
                            meta->accepted_socket.is_active = 0;
                        } else if (bytes_read == -1) {
                            // Read error - check if it's fatal
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                // Fatal error - log and close connection
                                count_socket_error(errno);
                                epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                close(client_fd);
                                meta->accepted_socket.is_active = 0;
                            }
                            // EAGAIN/EWOULDBLOCK are normal in non-blocking I/O - don't log them
                        }
                    } else {
                        // Successfully read data
                        meta->accepted_socket.bytes_received += bytes_read;
                        meta->total_bytes_received += bytes_read;
                        
                        // Echo back same amount of data - keep polling until all sent
                        size_t total_written = 0;
                        while (total_written < (size_t)bytes_read) {
                            ssize_t bytes_written = write(client_fd, buffer + total_written, 
                                                        bytes_read - total_written);
                            
                            if (bytes_written > 0) {
                                total_written += bytes_written;
                                meta->accepted_socket.bytes_sent += bytes_written;
                                meta->total_bytes_sent += bytes_written;
                            } else if (bytes_written == -1) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    // Socket buffer full - keep polling, no sleep
                                    continue;
                                } else {
                                    // Fatal write error - log and break
                                    count_socket_error(errno);
                                    printf("SERVER ERROR: %s - Write error on fd %d\n", 
                                           strerror(errno), client_fd);
                                    break;
                                }
                            } else {
                                // bytes_written == 0 - shouldn't happen with TCP, but break to avoid infinite loop
                                break;
                            }
                        }
                    }
                }

            }
        }
    }
    
    // Cleanup
    if (meta->accepted_socket.is_active) {
        close(meta->accepted_socket.socket_fd);
    }
    close(meta->listen_fd);
    close(meta->epoll_fd);
    
    return NULL;
}

int run_server(void) {
    // Server runs silently
    
    // Allocate server thread metadata
    g_ctx.server_threads = calloc(g_ctx.num_threads, sizeof(server_thread_meta_t));
    if (!g_ctx.server_threads) {
        perror("calloc");
        return -1;
    }
    
    // Create server threads
    for (int i = 0; i < g_ctx.num_threads; i++) {
        g_ctx.server_threads[i].thread_index = i;
        g_ctx.server_threads[i].port = g_ctx.listen_port_start + i;
        
        if (pthread_create(&g_ctx.server_threads[i].thread_id, NULL, 
                          server_thread_func, &g_ctx.server_threads[i]) != 0) {
            perror("pthread_create");
            return -1;
        }
    }
    
    // Server runs silently, no statistics display
    while (g_ctx.running) {
        sleep(1);  // Just keep main thread alive
    }
    
    // Wait for threads to finish
    for (int i = 0; i < g_ctx.num_threads; i++) {
        pthread_join(g_ctx.server_threads[i].thread_id, NULL);
    }
    
    return 0;
} 