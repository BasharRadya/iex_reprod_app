#include "network_app.h"

// Global connection counters for debugging
volatile int global_connections_accepted = 0;
volatile int global_connections_closed = 0;

void *server_thread_func(void *arg) {
    server_thread_meta_t *meta = (server_thread_meta_t *)arg;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct epoll_event event, events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    
    // Initialize connections array
    meta->active_connections = 0;
    for (int i = 0; i < MAX_CONNECTIONS_PER_THREAD; i++) {
        meta->accepted_sockets[i].is_active = 0;
        meta->accepted_sockets[i].socket_fd = -1;
        meta->accepted_sockets[i].bytes_received = 0;
        meta->accepted_sockets[i].bytes_sent = 0;
    }
    
    // Create listen socket
    meta->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (meta->listen_fd == -1) {
        perror("socket");
        exit(1);
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(meta->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        exit(1);
    }
    
    // Set non-blocking
    if (set_socket_nonblocking(meta->listen_fd) == -1) {
        exit(1);
    }
    
    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(g_ctx.listen_ip);
    server_addr.sin_port = htons(meta->port);
    
    if (bind(meta->listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(1);
    }
    
    // Listen
    if (listen(meta->listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        exit(1);
    }
    
    // Create epoll
    meta->epoll_fd = epoll_create1(0);
    if (meta->epoll_fd == -1) {
        perror("epoll_create1");
        exit(1);
    }
    
    // Add listen socket to epoll
    event.events = EPOLLIN;
    event.data.fd = meta->listen_fd;
    if (epoll_ctl(meta->epoll_fd, EPOLL_CTL_ADD, meta->listen_fd, &event) == -1) {
        perror("epoll_ctl add listen");
        exit(1);
    }
    
    printf("Server thread %d listening on port %d\n", meta->thread_index, meta->port);
    
    while (g_ctx.running) {
        int nfds = epoll_wait(meta->epoll_fd, events, MAX_EVENTS, 100);
        if (nfds == -1) {
            if (errno != EINTR) {
                perror("epoll_wait");
                exit(1);
            }
            continue;
        }
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == meta->listen_fd) {
                // New connection - find available slot
                int slot = -1;
                for (int j = 0; j < MAX_CONNECTIONS_PER_THREAD; j++) {
                    if (!meta->accepted_sockets[j].is_active) {
                        slot = j;
                        break;
                    }
                }
                
                if (slot == -1) {
                    // No slots available - accept and close immediately
                    int client_fd = accept(meta->listen_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd != -1) {
                        close(client_fd);
                        __sync_fetch_and_add(&global_connections_accepted, 1);
                        __sync_fetch_and_add(&global_connections_closed, 1);
                    }
                    continue;
                }
                
                // Accept new connection
                int client_fd = accept(meta->listen_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("SERVER THREAD %d: accept() got EAGAIN/EWOULDBLOCK\n", meta->thread_index);
                    } else {
                        perror("accept");
                        exit(1);
                    }
                    continue;
                }
                
                if (set_socket_nonblocking(client_fd) == -1) {
                    close(client_fd);
                    __sync_fetch_and_add(&global_connections_accepted, 1);
                    __sync_fetch_and_add(&global_connections_closed, 1);
                    exit(1);
                }
                
                // Store connection
                meta->accepted_sockets[slot].socket_fd = client_fd;
                meta->accepted_sockets[slot].bytes_received = 0;
                meta->accepted_sockets[slot].bytes_sent = 0;
                meta->accepted_sockets[slot].is_active = 1;
                meta->active_connections++;
                meta->total_accepts++;
                __sync_fetch_and_add(&global_connections_accepted, 1);
                
                // Add to epoll
                event.events = EPOLLIN;
                event.data.fd = client_fd;
                if (epoll_ctl(meta->epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl add client");
                    close(client_fd);
                    meta->accepted_sockets[slot].is_active = 0;
                    meta->active_connections--;
                    __sync_fetch_and_add(&global_connections_closed, 1);
                    exit(1);
                }
                
            } else {
                // Client socket event - find which connection
                int client_fd = events[i].data.fd;
                int slot = -1;
                for (int j = 0; j < MAX_CONNECTIONS_PER_THREAD; j++) {
                    if (meta->accepted_sockets[j].is_active && 
                        meta->accepted_sockets[j].socket_fd == client_fd) {
                        slot = j;
                        break;
                    }
                }
                
                if (slot == -1) {
                    epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    continue;
                }
                
                if (events[i].events & EPOLLIN) {
                    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
                    
                    if (bytes_read == 0) {
                        // Client closed connection
                        epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                        close(client_fd);
                        meta->accepted_sockets[slot].is_active = 0;
                        meta->active_connections--;
                        __sync_fetch_and_add(&global_connections_closed, 1);
                        
                    } else if (bytes_read == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            printf("SERVER THREAD %d: read() got EAGAIN/EWOULDBLOCK slot=%d, fd=%d\n", 
                                   meta->thread_index, slot, client_fd);
                            continue; // Normal for non-blocking
                        } else {
                            epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                            close(client_fd);
                            meta->accepted_sockets[slot].is_active = 0;
                            meta->active_connections--;
                            __sync_fetch_and_add(&global_connections_closed, 1);
                            exit(1);
                        }
                        
                    } else {
                        // Successfully read data - echo it back
                        meta->accepted_sockets[slot].bytes_received += bytes_read;
                        meta->total_bytes_received += bytes_read;
                        
                        // Send back exactly the same amount
                        size_t total_written = 0;
                        while (total_written < (size_t)bytes_read) {
                            ssize_t bytes_written = write(client_fd, buffer + total_written, 
                                                        bytes_read - total_written);
                            
                            if (bytes_written > 0) {
                                total_written += bytes_written;
                                meta->accepted_sockets[slot].bytes_sent += bytes_written;
                                meta->total_bytes_sent += bytes_written;
                                
                            } else if (bytes_written == -1) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    // Socket buffer full - keep trying
                                    printf("SERVER THREAD %d: write() got EAGAIN/EWOULDBLOCK slot=%d, fd=%d, sent=%zu/%zd\n", 
                                           meta->thread_index, slot, client_fd, total_written, bytes_read);
                                    continue;
                                } else {
                                    epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                    close(client_fd);
                                    meta->accepted_sockets[slot].is_active = 0;
                                    meta->active_connections--;
                                    __sync_fetch_and_add(&global_connections_closed, 1);
                                    exit(1);
                                }
                            } else {
                                epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                close(client_fd);
                                meta->accepted_sockets[slot].is_active = 0;
                                meta->active_connections--;
                                __sync_fetch_and_add(&global_connections_closed, 1);
                                exit(1);
                            }
                        }
                    }
                }
                
                // Check for other epoll events that indicate connection problems
                if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                    epoll_ctl(meta->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    meta->accepted_sockets[slot].is_active = 0;
                    meta->active_connections--;
                    __sync_fetch_and_add(&global_connections_closed, 1);
                }
            }
        }
    }
    
    // Cleanup
    for (int i = 0; i < MAX_CONNECTIONS_PER_THREAD; i++) {
        if (meta->accepted_sockets[i].is_active) {
            close(meta->accepted_sockets[i].socket_fd);
            __sync_fetch_and_add(&global_connections_closed, 1);
        }
    }
    close(meta->listen_fd);
    close(meta->epoll_fd);
    
    return NULL;
}

int run_server(void) {
    printf("Starting server with %d threads on ports %d-%d\n", 
           g_ctx.num_threads, g_ctx.listen_port_start, 
           g_ctx.listen_port_start + g_ctx.num_threads - 1);
    
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
    
    // Main loop - print global stats
    while (g_ctx.running) {
        sleep(2);
        printf("MAIN: Global connections - accepted=%d, closed=%d, active=%d\n", 
               global_connections_accepted, global_connections_closed, 
               global_connections_accepted - global_connections_closed);
    }
    
    // Wait for threads to finish
    for (int i = 0; i < g_ctx.num_threads; i++) {
        pthread_join(g_ctx.server_threads[i].thread_id, NULL);
    }
    
    return 0;
} 