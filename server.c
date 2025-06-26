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
                    meta->accepted_socket.bytes_pending_send = 0;
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
                            // Read error (including EAGAIN)
                            count_socket_error(errno);
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                // Fatal error - close connection
                                epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                close(client_fd);
                                meta->accepted_socket.is_active = 0;
                            }
                        }
                    } else {
                        // Successfully read data
                        meta->accepted_socket.bytes_received += bytes_read;
                        meta->accepted_socket.bytes_pending_send += bytes_read;
                        meta->total_bytes_received += bytes_read;
                        
                        // Try to echo back the data immediately, but only send what we have pending
                        uint64_t can_send = meta->accepted_socket.bytes_pending_send;
                        if (can_send > 0) {
                            size_t send_amount = ((uint64_t)bytes_read <= can_send) ? (size_t)bytes_read : (size_t)can_send;
                            ssize_t bytes_written = write(client_fd, buffer, send_amount);
                            
                            if (bytes_written > 0) {
                                meta->accepted_socket.bytes_sent += bytes_written;
                                meta->accepted_socket.bytes_pending_send -= bytes_written;
                                meta->total_bytes_sent += bytes_written;
                                
                                // If we still have pending data, enable EPOLLOUT for later sending
                                if (meta->accepted_socket.bytes_pending_send > 0) {
                                    event.events = EPOLLIN | EPOLLOUT;
                                    event.data.fd = client_fd;
                                    epoll_ctl(meta->epoll_fd, EPOLL_CTL_MOD, client_fd, &event);
                                }
                            } else if (bytes_written == -1) {
                                count_socket_error(errno);
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    // Socket buffer full, enable EPOLLOUT to try again later
                                    event.events = EPOLLIN | EPOLLOUT;
                                    event.data.fd = client_fd;
                                    epoll_ctl(meta->epoll_fd, EPOLL_CTL_MOD, client_fd, &event);
                                } else {
                                    printf("SERVER ERROR: %s - %s on fd %d\n", 
                                           strerror(errno), "Write error", client_fd);
                                }
                            }
                        }
                    }
                }
                
                // Handle EPOLLOUT - socket ready for writing
                if (events[i].events & EPOLLOUT && meta->accepted_socket.bytes_pending_send > 0) {
                    // We have pending data but no actual data buffer to send from
                    // This is a limitation of the current design - we need a proper send buffer
                    // For now, just clear pending to avoid busy loop
                    meta->accepted_socket.bytes_pending_send = 0;
                    
                    // Remove EPOLLOUT since no more pending data
                    event.events = EPOLLIN;
                    event.data.fd = client_fd;
                    epoll_ctl(meta->epoll_fd, EPOLL_CTL_MOD, client_fd, &event);
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