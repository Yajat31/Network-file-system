#include "naming_server.h"
#include "../Utils/common.h"
#include "trie.h"
#include "lru.h"
#include <signal.h>

StorageServer storage_servers[MAX_STORAGE_SERVERS];
int server_count = 0;
pthread_mutex_t server_mutex = PTHREAD_MUTEX_INITIALIZER;
CacheEntry lru_cache[CACHE_SIZE];
int cache_count = 0;
Filelock file_locks[MAX_PATHS];
int lock_count = 0;
int port;

TrieNode * trienodes;
int triCount = 0;
int root = 0;

ServerHealth* server_health;
pthread_mutex_t health_mutex = PTHREAD_MUTEX_INITIALIZER;

void set_nonblocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

void init_server_health(StorageServer* ss, int index){
    pthread_mutex_lock(&health_mutex);
    server_health[index].server = ss;
    server_health[index].failed_checks = 0;
    server_health[index].last_check = time(NULL);
    server_health[index].is_being_checked = 0;
    pthread_mutex_unlock(&health_mutex);
}

int test_server_connection(const char* ip, int port){
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        return 0;
    }

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };
    inet_pton(AF_INET, ip, &addr.sin_addr);
    
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if(result < 0){
        close(sock);
        return 0;
    }
    Command cmd;
    cmd.type = CMD_PING;
    send(sock, &cmd, sizeof(Command), 0);
    close(sock);

    return 1;
}

void handle_server_failure(ServerHealth* health){
    StorageServer* ss = health->server;
    if(ss->is_alive) {
        ss->is_alive = 0;
        log_message("Storage server %s:%d is down after %d failed checks", ss->server_details.ip, ss->server_details.port, health->failed_checks);
    }
}

void check_server_health(ServerHealth* health){
    pthread_mutex_lock(&health_mutex);
    if(health->is_being_checked){
        pthread_mutex_unlock(&health_mutex);
        return;
    }
    health->is_being_checked = 1;
    pthread_mutex_unlock(&health_mutex);

    StorageServer* ss = health->server;
    int is_responsive = 0;

    for(int i = 0; i < MAX_RETRIES && !is_responsive; i++){
        is_responsive = test_server_connection(ss->server_details.ip, ss->server_details.port);
        if(!is_responsive && i < MAX_RETRIES - 1){
            printf("Retrying connection to %s:%d\n", ss->server_details.ip, ss->server_details.port);
            sleep(1);
        }
    }

    pthread_mutex_lock(&health_mutex);
    if(is_responsive){
        if(health->failed_checks > 0){
            log_message("Storage server %s:%d is back online", ss->server_details.ip, ss->server_details.port);
        }
        health->failed_checks = 0;
        ss->is_alive = 1;
    }
    else {
        health->failed_checks++;
        if(health->failed_checks >= MAX_RETRIES){
            handle_server_failure(health);
        }
    }

    health->last_check = time(NULL);
    health->is_being_checked = 0;
    pthread_mutex_unlock(&health_mutex);

    Command cmd;
    cmd.type = CMD_BACKUP;
    if(ss->backups[0] != -1){
        strcpy(cmd.backups[0].ip, ss->backup_servers[0].ip);
        cmd.backups[0].port = ss->backup_servers[0].port;
    }
    else{
        cmd.backups[0].port = -1;
    }
    if(ss->backups[1] != -1){
        strcpy(cmd.backups[1].ip, ss->backup_servers[1].ip);
        cmd.backups[1].port = ss->backup_servers[1].port;
    }
    else{
        cmd.backups[1].port = -1;
    }
    int ss_connect = connect_to_server(ss->server_details.ip, ss->server_details.port);
    if(ss_connect < 0){
        close(ss_connect);
        log_message("Failed to connect to storage server %s:%d", ss->server_details.ip, ss->server_details.port);
    }
    else{
        send(ss_connect, &cmd, sizeof(Command), 0);
        close(ss_connect);
    }
}

void* monitor_server(void* arg){
    while(1){
        pthread_mutex_lock(&server_mutex);
        for(int i = 0; i < server_count; i++){
            time_t current_time = time(NULL);
            if(current_time - server_health[i].last_check > HEALTH_CHECK_INTERVAL){
                log_message("Checking server health for %s:%d", storage_servers[i].server_details.ip, storage_servers[i].server_details.port);
                check_server_health(&server_health[i]);
            }
        }
        pthread_mutex_unlock(&server_mutex);
        sleep(1);
    }
    return NULL;
}

void handle_storage_server_registration(int socket) {
    StorageServer ss;
    memset(&ss, 0, sizeof(StorageServer));

    recv(socket, &ss, sizeof(StorageServer), MSG_WAITALL);

    printf("Storage server registration\n");

    pthread_mutex_lock(&server_mutex);
    
    int server_idx = -1;
    for (int i = 0; i < server_count; i++) {
        if (strcmp(ss.server_details.ip, storage_servers[i].server_details.ip) == 0 && 
            ss.server_details.port == storage_servers[i].server_details.port) {
            server_idx = i;
            break;
        }
    }
    
    if (server_idx >= 0) {
        storage_servers[server_idx].is_alive = 1;
        pthread_mutex_unlock(&server_mutex);
        for(int i = 0; i < ss.path_count; i++){
            insert_path(ss.paths[i], ss.isFolder[i], server_idx);
        }
        log_message("Storage server %d is registering again: %s:%d", server_idx, ss.server_details.ip, ss.server_details.port);
    } else {
        storage_servers[server_count] = ss;
        storage_servers[server_count].is_alive = 1;
        
        storage_servers[server_count].backups[0] = -1;
        storage_servers[server_count].backups[1] = -1;

        if(server_count == 1){
            storage_servers[0].backups[0] = 1;
            storage_servers[0].backup_servers[0] = storage_servers[1].server_details;
            storage_servers[1].backups[0] = 1;
            storage_servers[1].backup_servers[0] = storage_servers[0].server_details;
        }
        else if(server_count == 2){
            storage_servers[0].backups[1] = 2;
            storage_servers[0].backup_servers[1] = storage_servers[2].server_details;
            storage_servers[1].backups[1] = 2;
            storage_servers[1].backup_servers[1] = storage_servers[2].server_details;

            storage_servers[2].backups[0] = 0;
            storage_servers[2].backup_servers[0] = storage_servers[0].server_details;
            storage_servers[2].backups[1] = 1;
            storage_servers[2].backup_servers[1] = storage_servers[0].server_details;
        }
        else if(server_count > 2) {
            storage_servers[server_count].backups[0] = server_count-1;
            storage_servers[server_count].backups[1] = server_count-2;
            storage_servers[server_count].backup_servers[0] = storage_servers[server_count-1].server_details;
            storage_servers[server_count].backup_servers[1] = storage_servers[server_count-2].server_details;
        }
        
        for (int i = 0; i < ss.path_count; i++) {
            insert_path(ss.paths[i], ss.isFolder[i], server_count);
        }
        server_count++;
        pthread_mutex_unlock(&server_mutex);
        log_message("Storage server %d registered: %s:%d", server_count, ss.server_details.ip, ss.server_details.port);
        init_server_health(&storage_servers[server_count-1], server_count - 1);
    }
}

void handle_storage_server_backup(int sockfd){
    printf("Storage server backup\n");
}

int get_random_alive_server() {
    int alive_servers[server_count];
    int alive_count = 0;

    for (int i = 0; i < server_count; i++) {
        if (storage_servers[i].is_alive) {
            alive_servers[alive_count++] = i;
        }
    }

    if (alive_count == 0) {
        return -1;
    }

    static int seed_initialized = 0;
    if (!seed_initialized) {
        srand(time(NULL));
        seed_initialized = 1;
    }
    
    int random_idx = rand() % alive_count;
    return alive_servers[random_idx];
}

void handle_client_read(int sockfd, Command cmd){
    log_message("Client requested command: CMD_READ for path %s", cmd.path);
    Response response;
    int server_idx = check_cache(cmd.path);
    int original_idx = server_idx;
    TrieNode* node;
    if(server_idx == -1){
        node = searchPath(cmd.path);
        if(node == NULL){
            if(server_idx == -1){
                response.error = ERR_PATH_NOT_FOUND;
                send(sockfd, &response, sizeof(Response), 0);
                return;
            }
        }
        server_idx = node->mainServer;
        original_idx = server_idx;
        update_cache(cmd.path, server_idx);
    }
    int is_alive = storage_servers[server_idx].is_alive;
    int isbackup = 0;
    if(is_alive == 0){
        int backup1 = storage_servers[server_idx].backups[0];
        int backup2 = storage_servers[server_idx].backups[1];
        if(backup1 != -1 && storage_servers[backup1].is_alive){
            server_idx = backup1;
            isbackup = 1;
        }
        else if(backup2 != -1 && storage_servers[backup2].is_alive){
            server_idx = backup2;
            isbackup = 1;
        }
        else{
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
    }
    if(isbackup){
        char final_path[512];
        strcpy(final_path, "./ss");
        char num[10];
        sprintf(num, "%d", original_idx+1);
        strcat(final_path, num);
        strcat(final_path, cmd.path);
        strcpy(cmd.path, final_path);
        printf("Final path: %s\n", cmd.path);
        cmd.isbackup = isbackup;
    }
    response.error = ACK;
    strcpy(response.ip, storage_servers[server_idx].server_details.ip);
    response.port = storage_servers[server_idx].server_details.port;
    send(sockfd, &response, sizeof(Response), 0);
    send(sockfd, &cmd, sizeof(Command), 0);
}

void handle_client_write(int sockfd, Command cmd){
    Response response = {0};
    log_message("Client requested command CMD_WRITE for path %s", cmd.path);
    int server_idx = check_cache(cmd.path);
    if(server_idx == -1){
        TrieNode* node = searchPath(cmd.path);
        if(node == NULL){
            response.error = ERR_PATH_NOT_FOUND;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
        server_idx = node->mainServer;
    }
    while(storage_servers[server_idx].is_alive == 0){
        if(storage_servers[server_idx].backups[0] != -1){
            server_idx = storage_servers[server_idx].backups[0];
        }
        else if(storage_servers[server_idx].backups[1] != -1){
            server_idx = storage_servers[server_idx].backups[1];
        }
        else{
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
    }
    response.error = ACK;
    strcpy(response.ip, storage_servers[server_idx].server_details.ip);
    response.port = storage_servers[server_idx].server_details.port;
    send(sockfd, &response, sizeof(Response), 0);
}

void handle_client_create(int sockfd, Command cmd){
    log_message("Client requested command: CMD_CREATE for path %s", cmd.path);
    int server_idx = check_cache(cmd.path);
    TrieNode* node;
    Response response;
    response.error = ACK;
    if(server_idx == -1){
        node = searchPath(cmd.path);
        if(node != NULL){
            server_idx = get_random_alive_server();
            if(server_idx == -1){
                response.error = ERR_SERVER_DOWN;
                send(sockfd, &response, sizeof(Response), 0);
                return;
            }
        }
        else{
            server_idx = node->mainServer;
        }
    }
    int ss_connect = connect_to_server(storage_servers[server_idx].server_details.ip, storage_servers[server_idx].server_details.port);
    if(ss_connect < 0){
        response.error = ERR_SERVER_DOWN;
        send(sockfd, &response, sizeof(Response), 0);
        return;
    }
    send(ss_connect, &cmd, sizeof(Command), 0);
    recv(ss_connect, &response, sizeof(Response), 0);
    printf("Response error: %d\n", response.error);
    char final_path[512];
    strcpy(final_path, cmd.path);
    if(strcmp(final_path, "/") != 0)
        strcat(final_path, "/");
    strcat(final_path, cmd.data);
    printf("Final path: %s\n", final_path);
    if(response.error == ACK){
        insert_path(final_path, !cmd.isFIle, server_idx);
        log_message("Path created: %s", final_path);
    }       
}

void handle_client_delete(int sockfd, Command cmd){
    log_message("Client requested command: CMD_DELETE for path %s", cmd.path);
    int server_idx = check_cache(cmd.path);
    TrieNode* node;
    if(server_idx == -1){
        node = searchPath(cmd.path);
        if(node == NULL){
            Response response = {0};
            response.error = ERR_PATH_NOT_FOUND;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
        server_idx = node->mainServer;
    }
    Response response = {0};
    if(server_idx == -1){
        response.error = ERR_PATH_NOT_FOUND;
        send(sockfd, &response, sizeof(Response), 0);
    }
    else{
        int server_idx = node->mainServer;
        if(storage_servers[server_idx].is_alive){
            
            int ss_connection = connect_to_server(storage_servers[server_idx].server_details.ip, storage_servers[server_idx].server_details.port);
            if(ss_connection < 0){
                response.error = ERR_SERVER_DOWN;
                send(sockfd, &response, sizeof(Response), 0);
                return;
            }
            send(ss_connection, &cmd, sizeof(Command), 0);
            recv(ss_connection, &response, sizeof(Response), 0);
            if(response.error == ACK){
                deletePath(cmd.path);
            }
            close(ss_connection);
            send(sockfd, &response, sizeof(Response), 0);
        }
        else{
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
        }
    }
}

void handle_client_copy(int sockfd, Command cmd){
    log_message("Client requested command: CMD_COPY for path %s", cmd.path);
    Response response;
    TrieNode* node = searchPath(cmd.path);
    int server_idx1;
    if(node == NULL){
        response.error = ERR_PATH_NOT_FOUND;
        send(sockfd, &response, sizeof(Response), 0);
        return;
    }
    else{
        server_idx1 = node->mainServer;
        update_cache(cmd.path, server_idx1);
        if(!storage_servers[server_idx1].is_alive){
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
    }
    int server_idx2;
    if(cmd.dest_path[0] != '/'){
        server_idx2 = get_random_alive_server();
        if(server_idx2 == -1){
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
    }
    else{
        TrieNode* node = searchPath(cmd.dest_path);
        if(node == NULL){
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
        else{
            server_idx2 = node->mainServer;
            if(storage_servers[server_idx2].is_alive == 0){
                server_idx2 = storage_servers[server_idx2].backups[0];
            }
        }
    }
    printf("Server 1: %d, Server 2: %d\n", server_idx1, server_idx2);
    int ss_connection = connect_to_server(storage_servers[server_idx1].server_details.ip, storage_servers[server_idx1].server_details.port);
    if(ss_connection < 0){
        response.error = ERR_SERVER_DOWN;
        send(sockfd, &response, sizeof(Response), 0);
        return;
    }
    send(ss_connection, &cmd, sizeof(Command), 0);
    response.error = ACK;
    strcpy(response.ip, storage_servers[server_idx2].server_details.ip);
    response.port = storage_servers[server_idx2].server_details.port;
    send(ss_connection, &response, sizeof(Response), 0);
    recv(ss_connection, &response, sizeof(Response), 0);
    if(response.error == ACK){
        log_message("Path copied from %s to %s", cmd.path, cmd.dest_path);
        printf("Path copied from %s to %s\n", cmd.path, cmd.dest_path);
    }
}

void handle_client_list(int sockfd, Command cmd){
    log_message("Client requested command: CMD_LIST for path %s", cmd.path);
    TrieNode* node = searchPath(cmd.path);
    Response response = {0};
    if(node == NULL){
        log_message("Path not found: %s", cmd.path);
        response.error = ERR_PATH_NOT_FOUND;
        send(sockfd, &response, sizeof(Response), 0);
        return;
    }
    else{
        log_message("Path found: %s", cmd.path);
        char* list = concatenateSubpaths(cmd.path);
        response.error = ACK;
        response.buffer_size = strlen(list)+1;
        send(sockfd, &response, sizeof(Response), 0);
        send(sockfd, list, strlen(list), 0);
    }
}

void handle_client_stream(int sockfd, Command cmd){
    printf("Client requested command: CMD_STREAM for path %s\n", cmd.path);
    Response response;
    int server_idx = check_cache(cmd.path);
    int original_idx = server_idx;
    TrieNode* node;
    if(server_idx == -1){
        node = searchPath(cmd.path);
        if(node == NULL){
            if(server_idx == -1){
                response.error = ERR_PATH_NOT_FOUND;
                send(sockfd, &response, sizeof(Response), 0);
                return;
            }
        }
        server_idx = node->mainServer;
        original_idx = server_idx;
        update_cache(cmd.path, server_idx);
    }
    int is_alive = storage_servers[server_idx].is_alive;
    int isbackup = 0;
    if(is_alive == 0){
        int backup1 = storage_servers[server_idx].backups[0];
        int backup2 = storage_servers[server_idx].backups[1];
        if(backup1 != -1 && storage_servers[backup1].is_alive){
            server_idx = backup1;
            isbackup = 1;
        }
        else if(backup2 != -1 && storage_servers[backup2].is_alive){
            server_idx = backup2;
            isbackup = 1;
        }
        else{
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
    }
    if(isbackup){
        char final_path[512];
        strcpy(final_path, "./ss");
        char num[10];
        sprintf(num, "%d", original_idx+1);
        strcat(final_path, num);
        strcat(final_path, "/");
        strcat(final_path, cmd.path);
        strcpy(cmd.path, final_path);
        printf("Final path: %s\n", cmd.path);
        cmd.isbackup = isbackup;
    }
    response.error = ACK;
    strcpy(response.ip, storage_servers[server_idx].server_details.ip);
    response.port = storage_servers[server_idx].server_details.port;
    send(sockfd, &response, sizeof(Response), 0);
    send(sockfd, &cmd, sizeof(Command), 0);
}

void handle_client_info(int sockfd, Command cmd){
    printf("Client requested command: CMD_STREAM for path %s\n", cmd.path);
    Response response;
    int server_idx = check_cache(cmd.path);
    int original_idx = server_idx;
    TrieNode* node;
    if(server_idx == -1){
        node = searchPath(cmd.path);
        if(node == NULL){
            if(server_idx == -1){
                response.error = ERR_PATH_NOT_FOUND;
                send(sockfd, &response, sizeof(Response), 0);
                return;
            }
        }
        server_idx = node->mainServer;
        original_idx = server_idx;
        update_cache(cmd.path, server_idx);
    }
    int is_alive = storage_servers[server_idx].is_alive;
    int isbackup = 0;
    if(is_alive == 0){
        int backup1 = storage_servers[server_idx].backups[0];
        int backup2 = storage_servers[server_idx].backups[1];
        if(backup1 != -1 && storage_servers[backup1].is_alive){
            server_idx = backup1;
            isbackup = 1;
        }
        else if(backup2 != -1 && storage_servers[backup2].is_alive){
            server_idx = backup2;
            isbackup = 1;
        }
        else{
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
    }
    if(isbackup){
        char final_path[512];
        strcpy(final_path, "./ss");
        char num[10];
        sprintf(num, "%d", original_idx+1);
        strcat(final_path, num);
        strcat(final_path, "/");
        strcat(final_path, cmd.path);
        strcpy(cmd.path, final_path);
        printf("Final path: %s\n", cmd.path);
        cmd.isbackup = isbackup;
    }
    response.error = ACK;
    strcpy(response.ip, storage_servers[server_idx].server_details.ip);
    response.port = storage_servers[server_idx].server_details.port;
    send(sockfd, &response, sizeof(Response), 0);
    send(sockfd, &cmd, sizeof(Command), 0);
}

void* handle_connection(void* arg){
    ThreadArgs* thread_args = (ThreadArgs*)arg;
    int client_socket = thread_args->socket;
    Command cmd;
    
    if(recv(client_socket, &cmd, sizeof(Command), 0) < 0){
        close(client_socket);
        free(thread_args);
        return NULL;
    }

    if(cmd.type == CMD_REGISTER){
        handle_storage_server_registration(client_socket);
    }
    else if(cmd.type == CMD_BACKUP){
        handle_storage_server_backup(client_socket);
    }
    else if(cmd.type == CMD_READ){
        handle_client_read(client_socket, cmd);   
    }
    else if(cmd.type == CMD_WRITE){
        handle_client_write(client_socket, cmd);
    }
    else if(cmd.type == CMD_CREATE){
        handle_client_create(client_socket, cmd);
    }
    else if(cmd.type == CMD_DELETE){
        handle_client_delete(client_socket, cmd);
    }
    else if(cmd.type == CMD_LIST){
        handle_client_list(client_socket, cmd);
    }
    else if(cmd.type == CMD_COPY){
        handle_client_copy(client_socket, cmd);
    }
    else if(cmd.type == CMD_STREAM){
        handle_client_stream(client_socket, cmd);
    }
    else if(cmd.type == CMD_INFO){
        handle_client_info(client_socket, cmd);
    }
    else{
        log_message("Invalid command type %d", cmd.type);
    }

    close(client_socket);
    free(thread_args);
    return NULL;
}

void acquire_file_lock(const char* path) {
    pthread_mutex_lock(&server_mutex);
    for (int i = 0; i < lock_count; i++) {
        if (strcmp(file_locks[i].path, path) == 0) {
            pthread_mutex_lock(&file_locks[i].lock);
            file_locks[i].in_use = 1;
            pthread_mutex_unlock(&server_mutex);
            return;
        }
    }
    
    // Create new lock
    strcpy(file_locks[lock_count].path, path);
    pthread_mutex_init(&file_locks[lock_count].lock, NULL);
    pthread_mutex_lock(&file_locks[lock_count].lock);
    file_locks[lock_count].in_use = 1;
    lock_count++;
    pthread_mutex_unlock(&server_mutex);
}

void release_file_lock(const char* path) {
    pthread_mutex_lock(&server_mutex);
    for (int i = 0; i < lock_count; i++) {
        if (strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].in_use = 0;
            pthread_mutex_unlock(&file_locks[i].lock);
            pthread_mutex_unlock(&server_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&server_mutex);
}



int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
    initializeTrie();
    server_health = calloc(MAX_STORAGE_SERVERS, sizeof(ServerHealth));

    port = atoi(argv[1]);
    int server_socket = create_socket();        
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_message("Bind failed: %s", strerror(errno));
        return 1;
    }
    
    listen(server_socket, SOMAXCONN);
    log_message("Naming server started on port %d", port);
    
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, monitor_server, NULL);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_socket < 0) {
            log_message("Accept failed: %s", strerror(errno));
            continue;
        }
        
        ThreadArgs* thread_args = malloc(sizeof(ThreadArgs));
        thread_args->socket = client_socket;
        thread_args->client_addr = client_addr;

        pthread_t thread_id;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if(pthread_create(&thread_id, &attr, handle_connection, thread_args) != 0){
            log_message("Failed to create thread: %s", strerror(errno));
            free(thread_args);
            close(client_socket);
        }

        pthread_attr_destroy(&attr);
    }

    close(server_socket);
    return 0;
}
