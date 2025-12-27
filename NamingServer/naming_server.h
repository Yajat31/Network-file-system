#ifndef NAMING_SERVER_H
#define NAMING_SERVER_H

#define _GNU_SOURCE
#include "../Utils/common.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

typedef struct {
    char path[256];
    int in_use;
    pthread_mutex_t lock;
} Filelock;

// Function declarations
int create_socket();
int connect_to_server(const char* ip, int port);
void send_with_retry(int socket, void* data, size_t size);
int recv_with_timeout(int socket, void* buffer, size_t size, int timeout_sec);
void log_message(const char* format, ...);
ErrorCode handle_error(int error_code);

int send_with_chunks_retry(int socket, const void* data, size_t size, int timeout_sec, int max_retries);
int recv_with_chunks_retry(int socket, void* buffer, size_t size, int timeout_sec, int max_retries);
#endif