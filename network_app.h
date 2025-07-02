#ifndef NETWORK_APP_H
#define NETWORK_APP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define MAX_THREADS 100
#define MAX_CONNECTIONS_PER_THREAD 1000

// Socket metadata for accepted connections
typedef struct {
    int socket_fd;
    uint64_t bytes_received;
    uint64_t bytes_sent;
    uint64_t bytes_pending_send;  // Bytes received but not yet fully sent back
    int is_active;
} accepted_socket_meta_t;

// Metadata for server listen sockets
typedef struct {
    int listen_fd;
    int epoll_fd;
    int thread_index;
    int port;
    uint64_t total_accepts;
    uint64_t total_bytes_received;  // Overall total across all connections
    uint64_t total_bytes_sent;      // Overall total across all connections
    accepted_socket_meta_t accepted_sockets[MAX_CONNECTIONS_PER_THREAD];
    int active_connections;
    pthread_t thread_id;
} server_thread_meta_t;

// Metadata for client connections
typedef struct {
    int socket_fd;
    int thread_index;
    int port;
    uint64_t reconnect_count;
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;       // Total bytes received across all iterations
    uint64_t current_iteration_sent;     // Bytes sent in current iteration
    uint64_t current_iteration_received; // Bytes received in current iteration
    int is_connected;
} client_connection_meta_t;

// Global context structure
typedef struct {
    // Input parameters
    int num_threads;
    int is_server;
    char listen_ip[16];
    int listen_port_start;
    uint64_t data_size_before_reconnect;
    int refresh_stats_seconds;
    
    // Socket error counters (abstract categories)
    uint64_t errors_connection;      // Connection-related errors (refused, reset, timeout)
    uint64_t errors_io;              // I/O errors (would block, broken pipe, not connected)
    uint64_t errors_system;          // System-level errors (interrupted, resource limits)
    uint64_t errors_other;           // All other unexpected errors
    
    // Server specific
    server_thread_meta_t *server_threads;
    
    // Client specific
    client_connection_meta_t *client_connections;
    int client_epoll_fd;
    
    // Control flags
    volatile int running;
} global_ctx_t;

// Global context variable
extern global_ctx_t g_ctx;

// Function declarations
void print_usage(const char *program_name);
int parse_arguments(int argc, char *argv[]);
int set_socket_nonblocking(int fd);
void *server_thread_func(void *arg);
int run_server(void);
int run_client(void);
void print_statistics(void);
void signal_handler(int sig);
void cleanup_resources(void);
void count_socket_error(int error_code);

#endif // NETWORK_APP_H 