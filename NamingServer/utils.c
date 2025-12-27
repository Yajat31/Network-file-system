#include "naming_server.h"
#include "../Utils/common.h"
#include <stdarg.h>
#include <sys/time.h>

void log_message(const char* format, ...) {
    time_t now;
    time(&now);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[24] = '\0';  // Remove newline

    FILE* log_file = fopen("nfs.log", "a");
    if (!log_file) return;

    fprintf(log_file, "[%s] ", timestamp);
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fclose(log_file);
}

int create_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        log_message("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    return sock;
}

int connect_to_server(const char* ip, int port) {
    int sock = create_socket();
    if (sock < 0) return -1;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = inet_addr(ip),
        .sin_port = htons(port)
    };

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_message("Failed to connect to %s:%d: %s", ip, port, strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

void send_with_retry(int socket, void* data, size_t size) {
    int retries = 3;
    while (retries > 0) {
        ssize_t sent = send(socket, data, size, 0);
        if (sent == size) return;
        
        if (sent < 0) {
            log_message("Send failed: %s. Retrying...", strerror(errno));
            retries--;
            sleep(1);
            continue;
        }
        
        // Partial send, continue with remaining data
        data += sent;
        size -= sent;
    }
    
    if (retries == 0) {
        log_message("Failed to send data after 3 retries");
    }
}

int recv_with_timeout(int socket, void* buffer, size_t size, int timeout_sec) {
    struct timeval tv = {
        .tv_sec = timeout_sec,
        .tv_usec = 0
    };
    
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    ssize_t received = recv(socket, buffer, size, 0);
    if (received < 0) {
        log_message("Receive failed: %s", strerror(errno));
    }
    
    return received;
}

int send_with_chunks_retry(int socket, const void* data, size_t size, int timeout_sec, int max_retries) {
    const char* data_ptr = (const char*)data; // Treat data as a byte array
    size_t sent = 0;
    int retries = 0;

    struct timeval tv = {
        .tv_sec = timeout_sec,
        .tv_usec = 0
    };
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)); // Set send timeout

    while (sent < size) {
        size_t chunk_size = (size - sent < 1024) ? (size - sent) : 1024; // Send in chunks
        ssize_t result = send(socket, data_ptr + sent, chunk_size, 0);

        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (++retries > max_retries) {
                    perror("Send timed out");
                    return -1; // Retry limit exceeded
                }
                printf("Send timed out, retrying (%d/%d)...\n", retries, max_retries);
                continue;
            }
            perror("Send failed");
            return -1; // Unrecoverable error
        }

        sent += result; // Advance pointer by the number of bytes sent
        retries = 0;    // Reset retries on successful send
    }

    return 0; // Success
}

int recv_with_chunks_retry(int socket, void* buffer, size_t size, int timeout_sec, int max_retries) {
    char* buffer_ptr = (char*)buffer; // Treat buffer as a byte array
    size_t received = 0;
    int retries = 0;

    struct timeval tv = {
        .tv_sec = timeout_sec,
        .tv_usec = 0
    };
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); // Set receive timeout

    while (received < size) {
        size_t chunk_size = (size - received < 1024) ? (size - received) : 1024; // Receive in chunks
        ssize_t result = recv(socket, buffer_ptr + received, chunk_size, 0);

        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (++retries > max_retries) {
                    perror("Receive timed out");
                    return -1; // Retry limit exceeded
                }
                printf("Receive timed out, retrying (%d/%d)...\n", retries, max_retries);
                continue;
            }
            perror("Receive failed");
            return -1; // Unrecoverable error
        } else if (result == 0) {
            printf("Connection closed by peer\n");
            return -1; // Peer closed the connection
        }

        received += result; // Advance pointer by the number of bytes received
        retries = 0;        // Reset retries on successful receive
    }

    return 0; // Success
}


ErrorCode handle_error(int error_code) {
    switch (error_code) {
        case ERR_PATH_NOT_FOUND:
            log_message("Error: File not found");
            break;
        case ERR_FILE_IN_USE:
            log_message("Error: File is currently in use");
            break;
        case ERR_PERMISSION_DENIED:
            log_message("Error: Permission denied");
            break;
        case ERR_SERVER_DOWN:
            log_message("Error: Storage server is down");
            break;
        case ERR_INVALID_PATH:
            log_message("Error: Invalid path");
            break;
        case ERR_NETWORK:
            log_message("Error: Network error");
            break;
        default:
            log_message("Unknown error: %d", error_code);
    }
    return error_code;
}