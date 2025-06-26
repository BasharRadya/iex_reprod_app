#include "network_app.h"

void count_socket_error(int error_code) {
    const char *error_name;
    const char *error_category;
    
    // Categorize and name the error
    switch (error_code) {
        // Connection-related errors
        case ECONNREFUSED:
            error_name = "ECONNREFUSED"; error_category = "Connection"; break;
        case ECONNRESET:
            error_name = "ECONNRESET"; error_category = "Connection"; break;
        case ETIMEDOUT:
            error_name = "ETIMEDOUT"; error_category = "Connection"; break;
        case ENOTCONN:
            error_name = "ENOTCONN"; error_category = "Connection"; break;
        case ECONNABORTED:
            error_name = "ECONNABORTED"; error_category = "Connection"; break;
        case ENETDOWN:
            error_name = "ENETDOWN"; error_category = "Connection"; break;
        case ENETUNREACH:
            error_name = "ENETUNREACH"; error_category = "Connection"; break;
        case EHOSTDOWN:
            error_name = "EHOSTDOWN"; error_category = "Connection"; break;
        case EHOSTUNREACH:
            error_name = "EHOSTUNREACH"; error_category = "Connection"; break;
            
        // I/O-related errors (expected in non-blocking operations)
        case EAGAIN:
            error_name = "EAGAIN"; error_category = "I/O"; break;
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
            error_name = "EWOULDBLOCK"; error_category = "I/O"; break;
#endif
        case EPIPE:
            error_name = "EPIPE"; error_category = "I/O"; break;
        case EBADF:
            error_name = "EBADF"; error_category = "I/O"; break;
        case EFAULT:
            error_name = "EFAULT"; error_category = "I/O"; break;
            
        // System-level errors
        case EINTR:
            error_name = "EINTR"; error_category = "System"; break;
        case ENOMEM:
            error_name = "ENOMEM"; error_category = "System"; break;
        case EMFILE:
            error_name = "EMFILE"; error_category = "System"; break;
        case ENFILE:
            error_name = "ENFILE"; error_category = "System"; break;
        case ENOBUFS:
            error_name = "ENOBUFS"; error_category = "System"; break;
        case ENOSPC:
            error_name = "ENOSPC"; error_category = "System"; break;
            
        default:
            error_name = "UNKNOWN"; error_category = "Other"; break;
    }
    
    // Count for client statistics, print for server
    if (g_ctx.is_server) {
        printf("SERVER ERROR: %s (%s) - %s\n", error_name, error_category, strerror(error_code));
        fflush(stdout);
    } else {
        // Client: just count for statistics
        switch (error_code) {
            case ECONNREFUSED: case ECONNRESET: case ETIMEDOUT: case ENOTCONN:
            case ECONNABORTED: case ENETDOWN: case ENETUNREACH: case EHOSTDOWN: case EHOSTUNREACH:
                g_ctx.errors_connection++; break;
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
            case EPIPE: case EBADF: case EFAULT:
                g_ctx.errors_io++; break;
            case EINTR: case ENOMEM: case EMFILE: case ENFILE: case ENOBUFS: case ENOSPC:
                g_ctx.errors_system++; break;
            default:
                g_ctx.errors_other++; break;
        }
    }
}

static int stats_lines = 0;

void print_statistics(void) {
    // Only show statistics for client
    if (g_ctx.is_server) {
        return;  // Server: silent mode, errors are printed as they happen
    }
    
    if (stats_lines > 0) {
        // Move cursor up to overwrite previous statistics
        printf("\033[%dA\033[J", stats_lines);
    }
    
    printf("=== Client Statistics ===\n");
    printf("Threads: %d | Refresh Rate: %d seconds\n\n", g_ctx.num_threads, g_ctx.refresh_stats_seconds);
    
    // Error statistics
    uint64_t total_errors = g_ctx.errors_connection + g_ctx.errors_io + 
                           g_ctx.errors_system + g_ctx.errors_other;
    
    printf("Socket Error Statistics (Total: %lu):\n", total_errors);
    if (total_errors > 0) {
        printf("  Connection Errors: %lu\n", g_ctx.errors_connection);
        printf("  I/O Errors:        %lu\n", g_ctx.errors_io);
        printf("  System Errors:     %lu\n", g_ctx.errors_system);
        printf("  Other Errors:      %lu\n", g_ctx.errors_other);
    } else {
        printf("  No errors detected\n");
    }
    printf("\n");
    
    printf("Client Connections:\n");
    printf("%-6s %-10s %-15s %-15s %-15s %-12s %-12s %-12s %-15s\n", 
           "Index", "Port", "Reconnects", "Total Sent", "Total Recv", "Iter Sent", "Iter Recv", "Socket FD", "Status");
    printf("----------------------------------------------------------------------------------------------------------------------\n");
    
    for (int i = 0; i < g_ctx.num_threads; i++) {
        client_connection_meta_t *conn = &g_ctx.client_connections[i];
        printf("%-6d %-10d %-15lu %-15lu %-15lu %-12lu %-12lu %-12d %-15s\n", 
               conn->thread_index, conn->port, conn->reconnect_count, 
               conn->total_bytes_sent, conn->total_bytes_received,
               conn->current_iteration_sent, conn->current_iteration_received, 
               conn->socket_fd, conn->is_connected ? "Connected" : "Disconnected");
    }
    
    printf("\n");
    
    // Count lines for next update (client only)
    stats_lines = 2;  // header: title + threads/refresh
    stats_lines += 1; // blank line
    stats_lines += 1; // error title
    if (total_errors > 0) {
        stats_lines += 4; // 4 error types
    } else {
        stats_lines += 1; // "no errors"
    }
    stats_lines += 1; // blank line
    stats_lines += 3; // client table: title + header + separator
    stats_lines += g_ctx.num_threads; // thread rows
    stats_lines += 1; // final newline
}

void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down gracefully...\n", sig);
    g_ctx.running = 0;
}

void cleanup_resources(void) {
    if (g_ctx.is_server && g_ctx.server_threads) {
        free(g_ctx.server_threads);
    }
    
    if (!g_ctx.is_server) {
        if (g_ctx.client_connections) {
            for (int i = 0; i < g_ctx.num_threads; i++) {
                if (g_ctx.client_connections[i].socket_fd != -1) {
                    close(g_ctx.client_connections[i].socket_fd);
                }
            }
            free(g_ctx.client_connections);
        }
        
        if (g_ctx.client_epoll_fd != -1) {
            close(g_ctx.client_epoll_fd);
        }
    }
} 