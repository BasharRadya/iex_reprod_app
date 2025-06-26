# IEX Reproduction Application (iex_reprod_app)

A high-performance client-server network application written in C designed for network stress testing and performance analysis. Features epoll-based I/O, multi-threaded server architecture, and comprehensive error reporting.

## Features

- **🚀 High-Performance Server**: Multi-threaded server with silent operation and real-time error reporting
- **📊 Monitoring Client**: Single-threaded client with detailed statistics and fixed-position display
- **⚡ Non-blocking I/O**: All sockets use epoll for maximum scalability
- **🔄 Reconnection Testing**: Client sends data, waits for echo, then reconnects (configurable data size)
- **📈 Comprehensive Statistics**: Real-time connection status, throughput, and error categorization
- **🔍 Smart Error Tracking**: Categorized error reporting (Connection/I/O/System/Other)

## Architecture

### Server
- **Silent Operation**: Server runs quietly with no console output except errors
- Creates one thread per configured port
- Each thread manages one listen socket and accepts one concurrent connection
- Uses epoll for efficient event-driven I/O
- Echoes back all received data

### Client
- Single-threaded design managing multiple connections
- One connection per server thread/port
- Sends random data and waits for complete echo before reconnecting
- **Smart Reconnection**: Tracks both sent and received bytes per iteration
- **Fixed-position Display**: Statistics update in place without scrolling
- Uses epoll for managing all connections simultaneously

## Building

```bash
# Build the application
make

# Clean build artifacts
make clean

# Debug build with symbols
make debug


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

### Server Output
- **Silent Operation**: No statistics displayed, only errors when they occur
- **Error Format**: `SERVER ERROR: <error_name> (<category>) - <description>`
- Examples: `SERVER ERROR: EAGAIN (I/O) - Resource temporarily unavailable`

### Client Output
- **Fixed-position Display**: Statistics update in place without scrolling
- **Per-connection Details**: Real-time connection progress and status
- **Error Tracking**: Categorized error counts with detailed breakdown

**Example Client Statistics:**
```
=== CLIENT STATISTICS ===
Index  Port       Reconnects      Total Sent      Total Recv      Iter Sent    Iter Recv    Socket FD    Status         
----------------------------------------------------------------------------------------------------------------------
0      8000       0               0               0               0            0            4            Disconnected   
1      8001       0               0               0               0            0            5            Disconnected   
2      8002       0               0               0               0            0            6            Disconnected   
3      8003       0               0               0               0            0            7            Disconnected   
4      8004       0               0               0               0            0            8            Disconnected   
5      8005       0               0               0               0            0            9            Disconnected   
6      8006       0               0               0               0            0            10           Disconnected   
7      8007       0               0               0               0            0            11           Disconnected   


```

**Field Descriptions:**
- **Total**: Total bytes sent/received across all reconnections
- **Current**: Bytes sent/received in current iteration
- **Reconnects**: Number of reconnection cycles completed
- **Error Categories**: Connection, I/O, System, and Other error types

## Protocol

1. **Server**: Listens on consecutive ports starting from the specified port
2. **Client**: Connects to each server port sequentially
3. **Data Flow**: Client sends random data → Server echoes back → Client reads and discards
4. **Reconnection**: Client closes and reopens connection after sending specified data amount

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
- Use `make clean && make` if seeing compilation errors

**Runtime Issues:**
- Verify ports are not in use by other applications
- Check firewall settings for the specified ports
- Ensure sufficient file descriptor limits (ulimit -n)
- For large data sizes, monitor memory usage and system limits

**Connection Issues:**
- Start server before client
- Verify IP and port parameters match between client and server
- Check network connectivity between client and server hosts
- Server runs silently - check for error messages if connections fail


## License

This is a high-performance network testing application. Use at your own discretion for network performance analysis and stress testing. 
