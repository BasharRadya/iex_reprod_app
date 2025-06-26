#include "network_app.h"

// Global context variable
global_ctx_t g_ctx = {0};

void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\nRequired Options (All modes):\n");
    printf("  -t, --threads <num>           Number of threads\n");
    printf("  -m, --mode <client|server>    Run as client or server\n");
    printf("  -i, --ip <IP>                 Listen/Connect IP address\n");
    printf("  -p, --port <port>             Listen/Connect port start number\n");
    printf("\nRequired Options (Client only):\n");
    printf("  -d, --data-size <bytes>       Data size before reconnect\n");
    printf("\nOptional Options:\n");
    printf("  -r, --refresh <seconds>       Refresh stats interval (default: 1)\n");
    printf("  -h, --help                    Show this help message\n");
    printf("\nExample Usage:\n");
    printf("  Server: %s -t 4 -m server -i 127.0.0.1 -p 8000\n", program_name);
    printf("  Client: %s -t 4 -m client -i 127.0.0.1 -p 8000 -d 1024 -r 2\n", program_name);
    printf("\nDescription:\n");
    printf("  Server will create 4 listen sockets on ports 8000-8003\n");
    printf("  Client will connect to each server port and send 1024 bytes before reconnecting\n");
    printf("  Statistics will refresh every 1 second (server) or 2 seconds (client)\n");
}

int parse_arguments(int argc, char *argv[]) {
    int required_args = 0;
    
    // Set defaults
    g_ctx.refresh_stats_seconds = 1;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -t/--threads requires a value\n");
                return -1;
            }
            g_ctx.num_threads = atoi(argv[++i]);
            if (g_ctx.num_threads <= 0 || g_ctx.num_threads > MAX_THREADS) {
                fprintf(stderr, "Error: Number of threads must be 1-%d\n", MAX_THREADS);
                return -1;
            }
            required_args++;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -m/--mode requires a value\n");
                return -1;
            }
            char *mode = argv[++i];
            if (strcmp(mode, "server") == 0) {
                g_ctx.is_server = 1;
            } else if (strcmp(mode, "client") == 0) {
                g_ctx.is_server = 0;
            } else {
                fprintf(stderr, "Error: Mode must be 'client' or 'server'\n");
                return -1;
            }
            required_args++;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--ip") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -i/--ip requires a value\n");
                return -1;
            }
            strncpy(g_ctx.listen_ip, argv[++i], sizeof(g_ctx.listen_ip) - 1);
            required_args++;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -p/--port requires a value\n");
                return -1;
            }
            g_ctx.listen_port_start = atoi(argv[++i]);
            if (g_ctx.listen_port_start <= 0 || g_ctx.listen_port_start > 65535) {
                fprintf(stderr, "Error: Port must be 1-65535\n");
                return -1;
            }
            required_args++;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data-size") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -d/--data-size requires a value\n");
                return -1;
            }
            g_ctx.data_size_before_reconnect = (uint64_t)atoll(argv[++i]);
            if (g_ctx.data_size_before_reconnect == 0) {
                fprintf(stderr, "Error: Data size must be greater than 0\n");
                return -1;
            }
            required_args++;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--refresh") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -r/--refresh requires a value\n");
                return -1;
            }
            g_ctx.refresh_stats_seconds = atoi(argv[++i]);
            if (g_ctx.refresh_stats_seconds <= 0) {
                fprintf(stderr, "Error: Refresh interval must be greater than 0\n");
                return -1;
            }
        } else {
            fprintf(stderr, "Error: Unknown argument '%s'\n", argv[i]);
            return -1;
        }
    }
    
    // Check required arguments based on mode
    int expected_args = 4; // -t, -m, -i, -p are always required
    if (!g_ctx.is_server) {
        expected_args = 5; // Client also needs -d
    }
    
    if (required_args < expected_args) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage(argv[0]);
        return -1;
    }
    
    // Validate client-specific requirements
    if (!g_ctx.is_server && g_ctx.data_size_before_reconnect == 0) {
        fprintf(stderr, "Error: Client mode requires -d/--data-size parameter\n");
        print_usage(argv[0]);
        return -1;
    }
    
    return 0;
}

int set_socket_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        count_socket_error(errno);
        perror("fcntl F_GETFL");
        return -1;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        count_socket_error(errno);
        perror("fcntl F_SETFL O_NONBLOCK");
        return -1;
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Parse arguments
    if (parse_arguments(argc, argv) != 0) {
        return 1;
    }
    
    g_ctx.running = 1;
    
    printf("Configuration:\n");
    printf("  Mode: %s\n", g_ctx.is_server ? "Server" : "Client");
    printf("  Threads: %d\n", g_ctx.num_threads);
    printf("  %s IP: %s\n", g_ctx.is_server ? "Listen" : "Connect", g_ctx.listen_ip);
    printf("  Port Range: %d-%d\n", g_ctx.listen_port_start, g_ctx.listen_port_start + g_ctx.num_threads - 1);
    if (!g_ctx.is_server) {
        printf("  Data Size Before Reconnect: %lu bytes\n", g_ctx.data_size_before_reconnect);
        printf("  Stats Refresh: %d seconds\n\n", g_ctx.refresh_stats_seconds);
    }
    
    int result;
    if (g_ctx.is_server) {
        result = run_server();
    } else {
        result = run_client();
    }
    
    cleanup_resources();
    
    return result;
} 