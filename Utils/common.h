#ifndef COMMON_H
#define COMMON_H
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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

#define HEALTH_CHECK_INTERVAL 5
#define MAX_RESPONSE_TIME 5
#define MAX_RETRIES 1
#define MAX_THREADS 100

#define MAX_BUFFER 4096
#define MAX_PATHS 1000
#define MAX_CLIENTS 100
#define MAX_STORAGE_SERVERS 20
#define CACHE_SIZE 100
#define CHUNK_SIZE 1024
#define AUDIO_BUFFER_SIZE 8192

#define COLOR_RESET "\033[0m"
#define DARK_RED "\033[0;31m"
#define DARK_GREEN "\033[0;32m"
#define DARK_YELLOW "\033[0;33m"
#define DARK_BLUE "\033[0;34m"
#define DARK_MAGENTA "\033[0;35m"
#define DARK_CYAN "\033[0;36m"
#define DARK_WHITE "\033[0;37m"

typedef struct {
    int socket;
    struct sockaddr_in client_addr;
} ThreadArgs;

typedef enum {
    CMD_READ,
    CMD_WRITE,
    CMD_CREATE,
    CMD_DELETE,
    CMD_COPY,
    CMD_LIST,
    CMD_STREAM,
    CMD_INFO,
    CMD_REGISTER,
    CMD_BACKUP,
    CMD_RECEIVE_BACKUP,
    CMD_RECEIVE_COPY,
    CMD_PING
} CommandType;

typedef enum {
    ACK = 0,
    ERR_PATH_NOT_FOUND = -1,
    ERR_FILE_IN_USE = -2,
    ERR_PERMISSION_DENIED = -3,
    ERR_SERVER_DOWN = -4,
    ERR_INVALID_PATH = -5,
    ERR_NETWORK = -6
} ErrorCode;

typedef struct {
    char ip[16];
    int port;
} ServerDetails;

typedef struct {
    ServerDetails server_details;
    int path_count;
    int is_alive;
    char paths[MAX_PATHS][256];
    bool isFolder[MAX_PATHS];
    ServerDetails backup_servers[2];
    int backups[2];
} StorageServer;

typedef struct {
    StorageServer* server;
    int failed_checks;
    time_t last_check;
    int is_being_checked;
} ServerHealth;

typedef struct {
    ErrorCode error;
    int buffer_size;
    char ip[16];
    int port;
} Response;

typedef struct {
    CommandType type;
    char path[256];
    char dest_path[256];  // For copy operations
    char data[MAX_BUFFER];
    int sync_write;       // For synchronous writes
    int size;            // For data size
    int isFIle;
    int isbackup;
    ServerDetails backups[2];
    ErrorCode error;
} Command;

#endif