#include "client.h"
#include <readline/readline.h>
#include <readline/history.h>

char* nm_ip;
int nm_port;

volatile int stop_stream = 0;

void send_file(int socket, const char* filename) {
    FILE *fp;
    char buffer[MAX_BUFFER];
    size_t bytes_read, total_sent = 0;
    long file_size;
    // Open file
    fp = fopen(filename, "rb");
    if (!fp) {
        perror("File open error");
        return;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Send filename
    size_t filename_len = strlen(filename) + 1;
    if (send(socket, &filename_len, sizeof(filename_len), 0) < 0) {
        perror("Filename length send failed");
        fclose(fp);
        return;
    }
    if (send(socket, filename, filename_len, 0) < 0) {
        perror("Filename send failed");
        fclose(fp);
        return;
    }

    // Send file size
    if (send(socket, &file_size, sizeof(file_size), 0) < 0) {
        perror("File size send failed");
        fclose(fp);
        return;
    }

    // Send file content
    while ((bytes_read = fread(buffer, 1, MAX_BUFFER, fp)) > 0) {
        ssize_t sent = send(socket, buffer, bytes_read, 0);
        if (sent < 0) {
            perror("File send failed");
            break;
        }
        total_sent += sent;
        printf("Sent %zu / %ld bytes\r", total_sent, file_size);
        fflush(stdout);
    }

    printf("\nFile transfer complete. Total bytes sent: %zu\n", total_sent);
    fclose(fp);
}

void print_help() {
    printf("\nAvailable commands:\n");
    printf("  ✅read <path>                   - Read a file\n");
    printf("  ✅write [--sync] <path> <local_path>       - Write to a file from local\n");
    printf("  ✅create -f/-d <path> <name>    - Create a file or directory\n");
    printf("  ✅delete <path>                 - Delete a file or directory\n");
    printf("  ✅copy <source> <dest>          - Copy a file or directory\n");
    printf("  ✅list <path>                   - List all accessible subpaths. If no path provided '/' is taken as given path.\n");
    printf("  ✅stream <path>                 - Stream an audio file\n");
    printf("  ✅info <path>                   - Get file information\n");
    printf("  ✅help                          - Show this help\n");
    printf("  ✅exit                          - Exit the client\n\n");
}

void handle_stream_response(int socket) {
    // Open pipe to music player (e.g., mpv)
    FILE* player = popen("mpv -", "w");
    if (!player) {
        printf("Failed to start music player\n");
        return;
    }

    char buffer[AUDIO_BUFFER_SIZE];
    int bytes_received;
    while ((bytes_received = recv(socket, buffer, AUDIO_BUFFER_SIZE, 0)) > 0) {
        if (bytes_received == 3 && memcmp(buffer, "EOS", 3) == 0) break;
        fwrite(buffer, 1, bytes_received, player);
        fflush(player);
    }

    pclose(player);
}

void receive_file(int socket) {
    char filename[256];
    char buffer[MAX_BUFFER];
    long file_size;
    ssize_t bytes_received;
    size_t total_received = 0;
    size_t filename_len;

    // Receive filename length
    if (recv(socket, &filename_len, sizeof(filename_len), 0) <= 0) {
        perror("Filename length receive failed");
        return;
    }

    // Receive filename
    if (recv(socket, filename, filename_len, 0) <= 0) {
        perror("Filename receive failed");
        return;
    }

    // Receive file size
    if (recv(socket, &file_size, sizeof(file_size), 0) <= 0) {
        perror("File size receive failed");
        return;
    }

    printf("Receiving file: %s (Size: %ld bytes)\n", filename, file_size);
    printf("File Contents:\n");
    printf("----------------------------\n");

    // Receive and print file contents
    while (total_received < file_size) {
        size_t to_receive = (file_size - total_received < MAX_BUFFER) ? 
                             file_size - total_received : MAX_BUFFER;

        bytes_received = recv(socket, buffer, to_receive, 0);
        
        if (bytes_received <= 0) {
            perror("Receive failed");
            break;
        }

        // Print received content
        buffer[bytes_received] = '\0';
        printf("%s", buffer);
        fflush(stdout);

        total_received += bytes_received;
        // printf("Received %zu / %ld bytes\r", total_received, file_size);
    }

    printf("\n----------------------------\n");
    printf("File transfer complete. Total bytes received: %zu\n", total_received);
}

void *monitor_stop_signal(void *arg) {
    printf("Press 'q' and Enter to stop the stream...\n");
    char input;
    while (1) {
        input = getchar();
        if (input == 'q' || input == 'Q') {
            stop_stream = 1; 
            break;
        }
    }
    return NULL;
}

void play_audio_stream(int socket) {
    stop_stream = 0;
	char buffer[MAX_BUFFER];
	ssize_t bytes_received;
	FILE *mpv_input = popen("mpv -", "w");
	if (mpv_input == NULL) {
		perror("ERROR opening MPV");
	}

    pthread_t stop_thread;
    if (pthread_create(&stop_thread, NULL, monitor_stop_signal, NULL) != 0) {
        perror("ERROR creating stop monitor thread");
        pclose(mpv_input);
        return;
    }

	while (!stop_stream && (bytes_received = recv(socket, buffer, MAX_BUFFER, 0)) > 0) {
		fwrite(buffer, 1, bytes_received, mpv_input);
	}
	if (bytes_received < 0) {
		perror("ERROR receiving data");
	}
	printf("Stream finished.\n");
	pclose(mpv_input);
    pthread_cancel(stop_thread);
    pthread_join(stop_thread, NULL);
}

void execute_command(char* command) {
    char* token = strtok(command, " ");
    if (!token) return;

    Command cmd = {0};
    StorageServer ss;

    if (strcmp(token, "help") == 0) {
        print_help();
        return;
    }

    if (strcmp(token, "exit") == 0) {
        exit(0);
    }

    if (strcmp(token, "read") == 0) {
        token = strtok(NULL, " ");
        if (!token) {
            printf("Usage: read <path>\n");
            return;
        }

        cmd.type = CMD_READ;
        strcpy(cmd.path, token);

        int nm_socket = connect_to_server(nm_ip, nm_port);
        if (nm_socket < 0) return;
        Response response;
        send(nm_socket, &cmd, sizeof(Command), 0);
        recv(nm_socket, &response, sizeof(Response), 0);
        recv(nm_socket, &cmd, sizeof(Command), 0);
        if(response.error == ERR_SERVER_DOWN){
            printf("Storage server is down\n");
            return;
        }
        int ss_socket = connect_to_server(response.ip, response.port);
        if (ss_socket < 0) return;
        send(ss_socket, &cmd, sizeof(Command), 0);
        receive_file(ss_socket);
        close(ss_socket);
        close(nm_socket);
        if(response.error == ERR_PATH_NOT_FOUND){
            printf("Path not found\n");
        }
        else if(response.error == ERR_PERMISSION_DENIED){
            printf("Permission denied\n");
        }
        else if(response.error == ERR_INVALID_PATH){
            printf("Invalid path\n");
        }
    }
    else if (strcmp(token, "write") == 0) {
        bool inSync = false;
        char path[512];
        char local_path[512];
        token = strtok(NULL, " ");
        if(!token){
            printf("Usage: write [--sync] <path> <local_path>\n");
            return;
        }
        if(!strcmp(token, "--sync")){
            inSync = true;
            token = strtok(NULL, " ");
            if(!token){
                printf("Usage: write [--sync] <path> <local_path>\n");
                return;
            }
            strcpy(path, token);
            token = strtok(NULL, " ");
            if(!token){
                printf("Usage: write [--sync] <path> <local_path>\n");
                return;
            }
            strcpy(local_path, token);
        }
        else{
            strcpy(path, token);
            token = strtok(NULL, " ");
            if(!token){
                printf("Usage: write [--sync] <path> <local_path>\n");
                return;
            }
            strcpy(local_path, token);
        }
        cmd.type = CMD_WRITE;
        strcpy(cmd.path, path);
        cmd.sync_write = inSync;
        int nm_socket = connect_to_server(nm_ip, nm_port);
        if (nm_socket < 0) return;
        send(nm_socket, &cmd, sizeof(Command), 0);
        Response response;
        recv(nm_socket, &response, sizeof(Response), 0);
        if(response.error == ERR_PATH_NOT_FOUND){
            printf("Path not found\n");
            return;
        }
        else if(response.error == ERR_SERVER_DOWN){
            printf("Storage server is down\n");
            return;
        }
        int ss_socket = connect_to_server(response.ip, response.port);
        if(ss_socket < 0){
            printf("Failed to connect to storage server\n");
            return;
        }
        send(ss_socket, &cmd, sizeof(Command), 0);
        recv(ss_socket, &response, sizeof(Response), 0);
        if(response.error == ACK) {
            if(!inSync){
                int child = fork();
                if(child == 0){
                    int fd = open("/dev/null", O_WRONLY);
                    dup2(fd, STDOUT_FILENO);
                    dup2(fd, STDERR_FILENO);
                    dup2(fd, STDIN_FILENO);
                    send_file(ss_socket, local_path);
                    close(ss_socket);
                    close(nm_socket);
                    _exit(0);
                }
                printf("Async write started for %s\n", path);
            }
            else{
                send_file(ss_socket, local_path);
                printf("Write complete for %s\n", path);
            }
        }
        close(ss_socket);
        close(nm_socket);
    }
    else if(strcmp(token, "create") == 0){
        token = strtok(NULL, " ");

        if(!token || token[0] != '-' || (token[1] != 'f' && token[1] != 'd')){
            printf("Usage: create -f/-d <path> <name>\n");
            return;
        }

        bool isFile = token[1] == 'f';
        char path[512];
        char name[512];
        
        token = strtok(NULL, " ");
        if (!token) {
            printf("Usage: create -f/-d <path> <name>\n");
            return;
        }
        strcpy(name, token);

        token = strtok(NULL, " ");
        if(token) {
            strcpy(path, name);
            strcpy(name, token);
        }
        else{
            strcpy(path, "/");
        }

        cmd.type = CMD_CREATE;
        strcpy(cmd.path, path);
        strcpy(cmd.data, name);
        cmd.size = strlen(name);
        cmd.isFIle = isFile;
        int nm_socket = connect_to_server(nm_ip, nm_port);
        if (nm_socket < 0) return;

        send(nm_socket, &cmd, sizeof(Command), 0);
        Response response;
        recv(nm_socket, &response, sizeof(Response), 0);
        close(nm_socket);
        if(response.error == ACK){
            printf("Created %s/%s\n", strcmp(path, "/") == 0 ? "" : path, name);
        }
        else if(response.error == ERR_PATH_NOT_FOUND){
            printf("Path not found\n");
        }
        else if(response.error == ERR_PERMISSION_DENIED){
            printf("Permission denied\n");
        }
        else if(response.error == ERR_SERVER_DOWN){
            printf("Storage server is down\n");
        }
        else if(response.error == ERR_INVALID_PATH){
            printf("Invalid path\n");
        }
        else{
            printf("Create failed (error %d)\n", response.error);
        }
    }
    else if(strcmp(token, "delete") == 0){
        token = strtok(NULL, " ");
        if (!token) {
            printf("Usage: delete <path>\n");
            return;
        }
        cmd.type = CMD_DELETE;
        strcpy(cmd.path, token);

        int nm_socket = connect_to_server(nm_ip, nm_port);
        if (nm_socket < 0) return;

        send(nm_socket, &cmd, sizeof(Command), 0);
        Response response;
        recv(nm_socket, &response, sizeof(Response), 0);
        if(response.error == ACK){
            printf("Deleted %s\n", cmd.path);
        }
        else if(response.error == ERR_PATH_NOT_FOUND){
            printf("Path not found\n");
        }
        else if(response.error == ERR_PERMISSION_DENIED){
            printf("Permission denied\n");
        }
        else if(response.error == ERR_SERVER_DOWN){
            printf("Storage server is down\n");
        }
        close(nm_socket);
    }
    else if(strcmp(token, "copy") == 0){
        token = strtok(NULL, " ");
        if(!token){
            printf("Usage: copy <source> <dest>\n");
            return;
        }
        strcpy(cmd.path, token);
        token = strtok(NULL, " ");
        if(!token){
            printf("Usage: copy <source> <dest>\n");
            return;
        }
        strcpy(cmd.dest_path, token);
        cmd.type = CMD_COPY;

        int nm_socket = connect_to_server(nm_ip, nm_port);
        if (nm_socket < 0) return;
        send(nm_socket, &cmd, sizeof(Command), 0);
        Response response;
        recv(nm_socket, &response, sizeof(Response), 0);
        close(nm_socket);
        if(response.error == ACK){
            printf("Copied %s -> %s\n", cmd.path, cmd.dest_path);
        }
        else if(response.error == ERR_PATH_NOT_FOUND){
            printf("Path not found\n");
        }
        else if(response.error == ERR_SERVER_DOWN){
            printf("Storage server is down\n");
        }
        else{
            printf("Copy failed (error %d)\n", response.error);
        }
    }
    else if(strcmp(token, "list") == 0) {
        token = strtok(NULL, " ");
        if(token){
            strcpy(cmd.path, token);
        }
        else{
            strcpy(cmd.path, "/");
        }
        cmd.type = CMD_LIST;

        int nm_socket = connect_to_server(nm_ip, nm_port);
        if (nm_socket < 0) return;

        send(nm_socket, &cmd, sizeof(Command), 0);
        Response response;
        recv(nm_socket, &response, sizeof(Response), MSG_WAITALL);
        if(response.error == ERR_PATH_NOT_FOUND){
            printf("Path not found\n");
            return;
        }
        else if(response.error == ACK){
            printf("Paths in %s:\n", cmd.path);
            char* buffer = malloc(response.buffer_size);
            recv(nm_socket, buffer, response.buffer_size, MSG_WAITALL);
            buffer[response.buffer_size-1] = '\0';
            printf("%s", buffer);
        }
    }
    else if(strcmp(token, "info") == 0){
        token = strtok(NULL, " ");
        if (!token) {
            printf("Usage: info <path>\n");
            return;
        }
        cmd.type = CMD_INFO;
        strcpy(cmd.path, token);

        int nm_socket = connect_to_server(nm_ip, nm_port);
        if (nm_socket < 0) return;

        send(nm_socket, &cmd, sizeof(Command), 0);
        Response response;
        recv(nm_socket, &response, sizeof(Response), 0);
        recv(nm_socket, &cmd, sizeof(Command), 0);
        if(response.error == ERR_PATH_NOT_FOUND){
            printf("Path not found\n");
            return;
        }
        else if(response.error == ERR_SERVER_DOWN){
            printf("Storage server is down\n");
            return;
        }
        else if(response.error == ACK){
            int ss_socket = connect_to_server(response.ip, response.port);
            if(ss_socket < 0){
                printf("Failed to connect to storage server\n");
                return;
            }
            send(ss_socket, &cmd, sizeof(Command), 0);
            recv(ss_socket, &response, sizeof(Response), 0);
            if(response.error == ERR_PERMISSION_DENIED){
                printf("Permission denied\n");
                return;
            }
            else{
                char* buffer;
                buffer = malloc(response.buffer_size);
                recv(ss_socket, buffer, response.buffer_size, 0);
                buffer[response.buffer_size-1] = '\0';
                printf("%s\n", buffer);
            }
            close(ss_socket);
        }
    }
    else if (strcmp(token, "stream") == 0) {
        token = strtok(NULL, " ");
        if (!token) {
            printf("Usage: stream <path>\n");
            return;
        }

        cmd.type = CMD_STREAM;
        strcpy(cmd.path, token);

        int nm_socket = connect_to_server(nm_ip, nm_port);
        if (nm_socket < 0) return;
        Response response;
        send(nm_socket, &cmd, sizeof(Command), 0);
        recv(nm_socket, &response, sizeof(Response), 0);
        recv(nm_socket, &cmd, sizeof(Command), 0);
        if(response.error == ERR_PATH_NOT_FOUND){
            printf("Path not found\n");
            return;
        }
        if(response.error == ERR_SERVER_DOWN){
            printf("Storage server is down\n");
            return;
        }

        int ss_socket = connect_to_server(response.ip, response.port);
        if (ss_socket < 0) return;
        send(ss_socket, &cmd, sizeof(Command), 0);
        play_audio_stream(ss_socket);
    }
    else{
        printf("Invalid command\n");
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <nm_ip> <nm_port>\n", argv[0]);
        return 1;
    }

    nm_ip = argv[1];
    nm_port = atoi(argv[2]);

    printf("Network File System Client\n");
    printf("Type 'help' for available commands\n");

    char* input;
    while ((input = readline("> ")) != NULL) {
        if (strlen(input) > 0) {
            add_history(input);
            execute_command(input);
        }
        free(input);
    }

    return 0;
}