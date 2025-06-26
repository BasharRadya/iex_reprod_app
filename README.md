# Network Application

A high-performance client-server network application written in C that uses epoll for scalable I/O operations and supports both multi-threaded server and single-threaded client modes.

## Features

- **Server Mode**: Multi-threaded server with one thread per listen socket
- **Client Mode**: Single-threaded client with multiple concurrent connections
- **Non-blocking Sockets**: All sockets use non-blocking I/O with epoll
- **Statistics Display**: Real-time statistics with configurable refresh rate
- **Error Tracking**: Global socket error counter with detailed error reporting
- **Reconnection Logic**: Client automatically reconnects after sending specified data size
- **Echo Protocol**: Server echoes back all received data to clients

## Architecture

### Server
- Creates one thread per configured port
- Each thread manages one listen socket and accepts one concurrent connection
- Uses epoll for efficient event-driven I/O
- Echoes back all received data
- Tracks statistics per thread and per accepted connection

### Client
- Single-threaded design managing multiple connections
- One connection per server thread/port
- Sends random data and reads echoed responses
- Automatically reconnects after sending specified data size
- Uses epoll for managing all connections simultaneously

## Building

```bash
# Build the application
make

# Clean build artifacts
make clean

# Debug build with symbols
make debug

# Optimized release build
make release

# Show build information
make info

# Show help
make help
```

## Usage

### Command Line Arguments

**Required:**
- `-t, --threads <num>`: Number of threads/connections
- `-m, --mode <client|server>`: Run as client or server
- `-i, --ip <IP>`: Listen IP address
- `-p, --port <port>`: Listen port start number
- `-d, --data-size <bytes>`: Data size before reconnect

**Optional:**
- `-r, --refresh <seconds>`: Statistics refresh interval (default: 1)
- `-h, --help`: Show help message

### Examples

**Start a server with 4 threads:**
```bash
./network_app -t 4 -m server -i 127.0.0.1 -p 8000 -d 1024
```
This creates 4 server threads listening on ports 8000-8003.

**Start a client with 4 connections:**
```bash
./network_app -t 4 -m client -i 127.0.0.1 -p 8000 -d 1024 -r 2
```
This creates 4 client connections to ports 8000-8003, each sending 1024 bytes before reconnecting.

## Statistics Display

The application displays real-time statistics that refresh at the configured interval:

### Server Statistics
- **Listen Sockets**: Thread ID, port, total accepts, file descriptor, status
- **Client Connections**: Thread ID, port, bytes received/sent, file descriptor, connection status

### Client Statistics
- **Connections**: Index, port, reconnect count, total bytes sent, current iteration bytes, file descriptor, connection status

### Global Statistics
- Total socket errors across all operations
- Current mode (Server/Client)
- Number of threads/connections
- Refresh rate

## Protocol

1. **Server**: Listens on consecutive ports starting from the specified port
2. **Client**: Connects to each server port sequentially
3. **Data Flow**: Client sends random data → Server echoes back → Client reads and discards
4. **Reconnection**: Client closes and reopens connection after sending specified data amount

## Error Handling

- All socket operations are non-blocking and handle EAGAIN/EWOULDBLOCK appropriately
- Global error counter tracks all socket-related errors
- Detailed error messages with perror() for debugging
- Graceful shutdown on SIGINT/SIGTERM

## Performance Features

- **Epoll**: Efficient event-driven I/O for handling multiple connections
- **Non-blocking I/O**: Prevents blocking on any single connection
- **Minimal Memory Allocation**: Fixed-size structures and buffers
- **Zero-Copy Echo**: Direct buffer echo in server without additional copies

## Signal Handling

- **SIGINT (Ctrl+C)**: Graceful shutdown with resource cleanup
- **SIGTERM**: Graceful shutdown with resource cleanup

## Technical Details

- **Language**: C99 standard
- **Threading**: POSIX threads (pthreads)
- **I/O Multiplexing**: Linux epoll
- **Socket Type**: TCP (SOCK_STREAM)
- **Address Family**: IPv4 (AF_INET)

## Limitations

- Linux-specific due to epoll usage
- Maximum 100 threads/connections (configurable via MAX_THREADS)
- IPv4 only
- Single client connection per server thread at a time

## Troubleshooting

**Build Issues:**
- Ensure gcc and pthread library are installed
- Check that all source files are present

**Runtime Issues:**
- Verify ports are not in use by other applications
- Check firewall settings for the specified ports
- Ensure sufficient file descriptor limits (ulimit -n)

**Connection Issues:**
- Start server before client
- Verify IP and port parameters match between client and server
- Check network connectivity between client and server hosts

## License

This is a demonstration application. Use at your own discretion. 