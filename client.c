#include "network_app.h"

int connect_to_server(client_connection_meta_t *conn) {
    struct sockaddr_in server_addr;
    
    conn->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->socket_fd == -1) {
        count_socket_error(errno);
        printf("CLIENT FATAL: socket() failed for connection %d to port %d: %s\n", 
               conn->thread_index, conn->port, strerror(errno));
        printf("CLIENT: Exiting due to socket creation failure\n");
        exit(1);
    }
    
    if (set_socket_nonblocking(conn->socket_fd) == -1) {
        printf("CLIENT FATAL: Failed to set socket non-blocking for connection %d\n", conn->thread_index);
        printf("CLIENT: Exiting due to socket configuration failure\n");
        close(conn->socket_fd);
        exit(1);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(g_ctx.listen_ip);
    server_addr.sin_port = htons(conn->port);
    
    int result = connect(conn->socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (result == -1 && errno != EINPROGRESS) {
        count_socket_error(errno);
        printf("CLIENT FATAL: connect() failed for connection %d to %s:%d: %s\n", 
               conn->thread_index, g_ctx.listen_ip, conn->port, strerror(errno));
        printf("CLIENT: Exiting due to connection failure\n");
        close(conn->socket_fd);
        exit(1);
    }
    
    conn->is_connected = (result == 0) ? 1 : 0;
    conn->current_iteration_sent = 0;
    conn->current_iteration_received = 0;
    return 0;
}

int run_client(void) {
    printf("Starting client with %d connections to %s:%d-%d\n", 
           g_ctx.num_threads, g_ctx.listen_ip, g_ctx.listen_port_start, 
           g_ctx.listen_port_start + g_ctx.num_threads - 1);
    
    // Allocate client connection metadata
    g_ctx.client_connections = calloc(g_ctx.num_threads, sizeof(client_connection_meta_t));
    if (!g_ctx.client_connections) {
        perror("calloc");
        return -1;
    }
    
    // Create epoll
    g_ctx.client_epoll_fd = epoll_create1(0);
    if (g_ctx.client_epoll_fd == -1) {
        count_socket_error(errno);
        perror("epoll_create1");
        return -1;
    }
    
    // Initialize connections
    for (int i = 0; i < g_ctx.num_threads; i++) {
        g_ctx.client_connections[i].thread_index = i;
        g_ctx.client_connections[i].port = g_ctx.listen_port_start + i;
        g_ctx.client_connections[i].socket_fd = -1;
        
        // connect_to_server will exit on failure, so this always succeeds
        connect_to_server(&g_ctx.client_connections[i]);
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLOUT;
        event.data.ptr = &g_ctx.client_connections[i];
        epoll_ctl(g_ctx.client_epoll_fd, EPOLL_CTL_ADD, 
                 g_ctx.client_connections[i].socket_fd, &event);
    }
    
    struct epoll_event events[MAX_EVENTS];
    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];
    time_t last_stats_time = time(NULL);
    
    // Fill send buffer with a simple pattern (much faster than random)
    memset(send_buffer, 0xAA, BUFFER_SIZE);
    
    while (g_ctx.running) {
        int nfds = epoll_wait(g_ctx.client_epoll_fd, events, MAX_EVENTS, 100);
        if (nfds == -1) {
            if (errno != EINTR) {
                count_socket_error(errno);
                perror("epoll_wait");
            }
            continue;
        }
        
        for (int i = 0; i < nfds; i++) {
            client_connection_meta_t *conn = (client_connection_meta_t *)events[i].data.ptr;
            
            if (events[i].events & EPOLLOUT) {
                if (!conn->is_connected) {
                    // Check if connection is now established
                    int error = 0;
                    socklen_t len = sizeof(error);
                    if (getsockopt(conn->socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                        conn->is_connected = 1;
                    } else {
                        count_socket_error(error);
                        printf("CLIENT FATAL: Connection failed to establish for connection %d to %s:%d: %s\n", 
                               conn->thread_index, g_ctx.listen_ip, conn->port, strerror(error));
                        printf("CLIENT: Exiting due to connection establishment failure\n");
                        exit(1);
                    }
                }
                
                // Send data (only if we haven't sent enough yet)
                if (conn->current_iteration_sent < g_ctx.data_size_before_reconnect) {
                    uint64_t to_send = BUFFER_SIZE;
                    if (conn->current_iteration_sent + to_send > g_ctx.data_size_before_reconnect) {
                        to_send = g_ctx.data_size_before_reconnect - conn->current_iteration_sent;
                    }
                    
                    ssize_t bytes_sent = write(conn->socket_fd, send_buffer, (size_t)to_send);
                    if (bytes_sent > 0) {
                        conn->current_iteration_sent += bytes_sent;
                        conn->total_bytes_sent += bytes_sent;
                    } else if (bytes_sent == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        count_socket_error(errno);
                    }
                }
                
                // Remove reconnection logic from EPOLLOUT - moved to EPOLLIN
            }
            
            if (events[i].events & EPOLLIN) {
                // Read echoed data and track it
                ssize_t bytes_read = read(conn->socket_fd, recv_buffer, sizeof(recv_buffer));
                if (bytes_read <= 0) {
                    if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                        // Connection closed or error
                        epoll_ctl(g_ctx.client_epoll_fd, EPOLL_CTL_DEL, conn->socket_fd, NULL);
                        close(conn->socket_fd);
                        conn->reconnect_count++;
                        conn->is_connected = 0;
                        if (bytes_read == -1) {
                            count_socket_error(errno);
                        }
                        // connect_to_server will exit on failure, so this always succeeds
                        connect_to_server(conn);
                        struct epoll_event event;
                        event.events = EPOLLIN | EPOLLOUT;
                        event.data.ptr = conn;
                        epoll_ctl(g_ctx.client_epoll_fd, EPOLL_CTL_ADD, conn->socket_fd, &event);
                    }
                } else {
                    // Track received bytes
                    conn->current_iteration_received += bytes_read;
                    conn->total_bytes_received += bytes_read;
                    
                    // Check if we need to reconnect - close when received amount equals target
                    if (conn->current_iteration_received >= g_ctx.data_size_before_reconnect) {
                        epoll_ctl(g_ctx.client_epoll_fd, EPOLL_CTL_DEL, conn->socket_fd, NULL);
                        close(conn->socket_fd);
                        conn->reconnect_count++;
                        conn->is_connected = 0;
                        // connect_to_server will exit on failure, so this always succeeds
                        connect_to_server(conn);
                        struct epoll_event event;
                        event.events = EPOLLIN | EPOLLOUT;
                        event.data.ptr = conn;
                        epoll_ctl(g_ctx.client_epoll_fd, EPOLL_CTL_ADD, conn->socket_fd, &event);
                    }
                }
            }
        }
        
        // Print statistics
        time_t current_time = time(NULL);
        if (current_time - last_stats_time >= g_ctx.refresh_stats_seconds) {
            print_statistics();
            last_stats_time = current_time;
        }
    }
    
    return 0;
} 