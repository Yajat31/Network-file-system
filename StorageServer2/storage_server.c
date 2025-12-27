#include "storage_server.h"
#include "../Utils/common.h"
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#include <dirent.h>


StorageServer my_info;
int nm_port;
char nm_ip[16];

char root_path[512];

int create_zip(const char *folder_to_zip, const char *output_zip_file) {
    if (!folder_to_zip || !output_zip_file) {
        fprintf(stderr, "Error: Invalid folder or output file path.\n");
        return -1;
    }
    pid_t pid = fork();
    // printf("Folder to zip: %s\n", folder_to_zip);
    struct stat path_stat;
    stat(folder_to_zip, &path_stat);
    if (S_ISDIR(path_stat.st_mode)) {
        strcat(folder_to_zip, "/");
    }
    

    if (pid < 0) {
        perror("Fork failed");
        return -1;
    } else if (pid == 0) {
        //redirect STDIN, STDERR nad STDOUT to /dev/null
        int fd = open("/dev/null", O_WRONLY);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        dup2(fd, STDIN_FILENO);
        execlp("zip", "zip", "-r", output_zip_file, folder_to_zip, (char *)NULL);

        perror("execlp failed");
        exit(1);
    } else {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid failed");
            return -1;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // printf("Successfully created ZIP file: %s\n", output_zip_file);
            return 0;
        } else {
            fprintf(stderr, "Error: Failed to create ZIP file.\n");
            return -1;
        }
    }
}

int unzip_and_delete_in_place(const char *zip_file) {
    if (!zip_file) {
        fprintf(stderr, "Error: Invalid zip file path.\n");
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return -1;
    } else if (pid == 0) {
        // Child process: Unzip the file in place
        execlp("unzip", "unzip","-uo", "-q", zip_file, (char *)NULL);

        perror("execlp failed");
        exit(1);
    } else {
        // Parent process: Wait for the child process to complete
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid failed");
            return -1;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // printf("Successfully unzipped file: %s\n", zip_file);

            if (unlink(zip_file) == 0) {
                // printf("Successfully deleted ZIP file: %s\n", zip_file);
                return 0;
            } else {
                perror("Failed to delete ZIP file");
                return -1;
            }
        } else {
            fprintf(stderr, "Error: Failed to unzip file.\n");
            return -1;
        }
    }
}

int copy_file(const char *src_path, const char *dest_path) {
    // Open source file
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        perror("Failed to open source file");
        return -1;
    }

    // Get source file stats to preserve permissions
    struct stat src_stat;
    if (fstat(src_fd, &src_stat) < 0) {
        perror("Failed to get source file stats");
        close(src_fd);
        return -1;
    }

    // Open destination file with same permissions as source
    int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, src_stat.st_mode);
    if (dest_fd < 0) {
        perror("Failed to open destination file");
        close(src_fd);
        return -1;
    }

    // Copy file contents
    char buffer[4096];
    ssize_t bytes_read, bytes_written;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("Write error");
            close(src_fd);
            close(dest_fd);
            return -1;
        }
    }

    // Handle read error
    if (bytes_read < 0) {
        perror("Read error");
        close(src_fd);
        close(dest_fd);
        return -1;
    }

    // Close file descriptors
    close(src_fd);
    close(dest_fd);

    // Preserve file metadata (ownership, timestamps)
    if (chmod(dest_path, src_stat.st_mode) < 0) {
        perror("Failed to set destination file permissions");
    }

    return 0;
}

int copy_directory(const char *src_path, const char *dest_path) {
    DIR *src_dir = opendir(src_path);
    if (!src_dir) {
        perror("Failed to open source directory");
        return -1;
    }

    // Get source directory stats to preserve permissions
    struct stat src_stat;
    if (stat(src_path, &src_stat) < 0) {
        perror("Failed to get source directory stats");
        closedir(src_dir);
        return -1;
    }

    // Create destination directory with same permissions
    if (mkdir(dest_path, src_stat.st_mode) < 0 && errno != EEXIST) {
        perror("Failed to create destination directory");
        closedir(src_dir);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(src_dir)) != NULL) {
        // Skip current and parent directory entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Construct full source and destination paths
        char src_entry_path[PATH_MAX];
        char dest_entry_path[PATH_MAX];
        
        snprintf(src_entry_path, sizeof(src_entry_path), "%s/%s", src_path, entry->d_name);
        snprintf(dest_entry_path, sizeof(dest_entry_path), "%s/%s", dest_path, entry->d_name);

        // Get entry stats
        struct stat entry_stat;
        if (stat(src_entry_path, &entry_stat) < 0) {
            perror("Failed to stat source entry");
            continue;  // Skip problematic entry
        }

        // Recursively copy based on file type
        if (S_ISDIR(entry_stat.st_mode)) {
            if (copy_directory(src_entry_path, dest_entry_path) < 0) {
                fprintf(stderr, "Failed to copy directory: %s\n", src_entry_path);
            }
        } else if (S_ISREG(entry_stat.st_mode)) {
            if (copy_file(src_entry_path, dest_entry_path) < 0) {
                fprintf(stderr, "Failed to copy file: %s\n", src_entry_path);
            }
        }
    }

    closedir(src_dir);

    // Preserve directory metadata
    if (chmod(dest_path, src_stat.st_mode) < 0) {
        perror("Failed to set destination directory permissions");
    }

    return 0;
}

int copy_path(const char *src_path, const char *dest_path) {
    struct stat src_stat;
    if (stat(src_path, &src_stat) < 0) {
        perror("Failed to stat source path");
        return -1;
    }

    if (S_ISDIR(src_stat.st_mode)) {
        char *last_slash = strrchr(src_path, '/');
        char new_dest_path[PATH_MAX];
        strcpy(new_dest_path, dest_path);
        strcat(new_dest_path, "/");
        strcat(new_dest_path, last_slash + 1);
        return copy_directory(src_path, new_dest_path);
    } else if (S_ISREG(src_stat.st_mode)) {
        char *last_slash = strrchr(src_path, '/');
        char new_dest_path[PATH_MAX];
        strcpy(new_dest_path, dest_path);
        strcat(new_dest_path, "/");
        strcat(new_dest_path, last_slash + 1);
        return copy_file(src_path, new_dest_path);
    } else {
        fprintf(stderr, "Unsupported file type for path: %s\n", src_path);
        return -1;
    }
}

void register_ss(){
    int sockfd = connect_to_server(nm_ip, nm_port);
    if (sockfd < 0) exit(1);
    Command cmd = {0};
    cmd.type = CMD_REGISTER;
    send(sockfd, &cmd, sizeof(Command), 0);
    send(sockfd, &my_info, sizeof(StorageServer), 0);
    close(sockfd);
}

void scan_directory(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        char *str;
        str = malloc(512);
        strcpy(str, full_path);
        size_t prefixLen = strlen(root_path);
        if (strncmp(str, root_path, prefixLen) == 0) {
            memmove(str, str + prefixLen, strlen(str + prefixLen) + 1);
        }
        
        if (my_info.path_count < MAX_PATHS) {
            strcpy(my_info.paths[my_info.path_count],str);
            if(entry->d_type == DT_DIR){
                my_info.isFolder[my_info.path_count] = true;
            }
            else{
                my_info.isFolder[my_info.path_count] = false;
            }
            my_info.path_count++;
        }

        if (entry->d_type == DT_DIR) {
            scan_directory(full_path);
        }
    }
    closedir(dir);
}

int get_file_size(const char* filename) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        perror("Error checking file");
        return -1;
    }
    return st.st_size;
}

int send_file(int socket, const char* filename) {
    FILE *fp;
    char buffer[MAX_BUFFER];
    size_t bytes_read, total_sent = 0;
    long file_size;

    // Open file
    fp = fopen(filename, "rb");
    if (!fp) {
        perror("File open error");
        return -1;
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
        return -1;
    }
    if (send(socket, filename, filename_len, 0) < 0) {
        perror("Filename send failed");
        fclose(fp);
        return -1;
    }

    // Send file size
    if (send(socket, &file_size, sizeof(file_size), 0) < 0) {
        perror("File size send failed");
        fclose(fp);
        return -1;
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
    return 0;
}

void receive_file(int socket) {
    char filename[256];
    char buffer[MAX_BUFFER];
    size_t filename_len;
    long file_size, total_received = 0;
    FILE *fp;

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

    printf("Receiving file: %s\n", filename);

    // Open file for writing
    fp = fopen(filename, "wb");
    if (!fp) {
        perror("File open error");
        return;
    }

    // Receive file size
    if (recv(socket, &file_size, sizeof(file_size), 0) <= 0) {
        perror("File size receive failed");
        fclose(fp);
        return;
    }

    printf("File size: %ld bytes\n", file_size);

    // Receive file content
    while (total_received < file_size) {
        ssize_t bytes_received = recv(socket, buffer, MAX_BUFFER, 0);
        if (bytes_received <= 0) {
            if (bytes_received < 0)
                perror("File receive failed");
            break;
        }

        fwrite(buffer, 1, bytes_received, fp);
        total_received += bytes_received;
        printf("Received %ld / %ld bytes\r", total_received, file_size);
        fflush(stdout);
    }

    if (total_received == file_size) {
        printf("\nFile transfer complete. Total bytes received: %ld\n", total_received);
    } else {
        printf("\nFile transfer incomplete. Total bytes received: %ld\n", total_received);
    }

    fclose(fp);
}

void receive_and_save_file(int socket, char* file_path) {
    char filename[256];
    char buffer[MAX_BUFFER];
    size_t filename_len;
    long file_size, total_received = 0;
    FILE *fp;

    if (recv(socket, &filename_len, sizeof(filename_len), 0) <= 0) {
        perror("Filename length receive failed");
        return;
    }

    if (recv(socket, filename, filename_len, 0) <= 0) {
        perror("Filename receive failed");
        return;
    }
    printf("Receiving file: %s\n", filename);

    fp = fopen(file_path, "wb");
    if (!fp) {
        perror("File open error");
        return;
    }

    if (recv(socket, &file_size, sizeof(file_size), 0) <= 0) {
        perror("File size receive failed");
        fclose(fp);
        return;
    }
    printf("File size: %ld bytes\n", file_size);

    while (total_received < file_size) {
        ssize_t bytes_received = recv(socket, buffer, MAX_BUFFER, 0);
        if (bytes_received <= 0) {
            if (bytes_received < 0)
                perror("File receive failed");
            break;
        }
        fwrite(buffer, 1, bytes_received, fp);
        total_received += bytes_received;
        printf("Received %ld / %ld bytes\r", total_received, file_size);
        fflush(stdout);
    }

    if (total_received == file_size) {
        printf("\nFile transfer complete. Total bytes received: %ld\n", total_received);
    } else {
        printf("\nFile transfer incomplete. Total bytes received: %ld\n", total_received);
    }
    fclose(fp);
}

void handle_storage_server_backup(int sockfd, Command cmd){   
    char zip_file[512];
    strcpy(zip_file, "backup");
    strcat(zip_file, my_info.server_details.ip);
    my_info.backup_servers[0] = cmd.backups[0];
    my_info.backup_servers[1] = cmd.backups[1];
    //convert to string
    char sport[6];
    sprintf(sport, "%d", my_info.server_details.port);
    strcat(zip_file, sport);
    strcat(zip_file, ".zip");
    create_zip(root_path, zip_file);
    
    cmd.type = CMD_RECEIVE_BACKUP;
    if(cmd.backups[0].port != -1) {
        int ss1_sock = connect_to_server(my_info.backup_servers[0].ip, my_info.backup_servers[0].port);
        if(ss1_sock < 0){
            return;
        }
        send(ss1_sock, &cmd, sizeof(Command), 0);
        send_file(ss1_sock, zip_file);
    }
    if(cmd.backups[1].port != -1) {
        int ss2_sock = connect_to_server(my_info.backup_servers[1].ip, my_info.backup_servers[1].port);
        if(ss2_sock < 0){
            return;
        }
        send(ss2_sock, &cmd, sizeof(Command), 0);
        send_file(ss2_sock, zip_file);
    }
    remove(zip_file);
}

void handle_client_read(int sockfd, Command cmd){
    char final_path[512];
    //print
    printf("Backup: %d\n", cmd.isbackup);
    printf("Client requested command: CMD_READ for path %s\n", cmd.path);
    if(cmd.isbackup == 1){
        strcpy(final_path, cmd.path);
    }
    else {
        strcpy(final_path, root_path);
        strcat(final_path, cmd.path);
    }
    Response response;
    printf("Final path: %s\n", final_path);
    if(send_file(sockfd, final_path) == -1){
        response.error = ERR_INVALID_PATH;
        send(sockfd, &response, sizeof(Response), 0);
        return;
    }
    else{
        response.error = ACK;
        send(sockfd, &response, sizeof(Response), 0);
    }
}

void handle_client_write(int sockfd, Command cmd){
    log_message("Client requested command: CMD_WRITE for path %s", cmd.path);
    char final_path[512];
    strcpy(final_path, root_path);
    if(cmd.path[0] != '/')
        strcat(final_path, "/");
    strcat(final_path, cmd.path);
    Response response;
    response.error = ACK;
    send(sockfd, &response, sizeof(Response), 0);
    receive_and_save_file(sockfd, final_path);
}

void handle_nm_create(int sockfd, Command cmd){
    Response response;
    char file_path[512];
    strcpy(file_path, root_path);
    if(cmd.path[0] != '/')
        strcat(file_path, cmd.path);
    strcat(file_path, "/");

    char* new_path = malloc(cmd.size);
    strcpy(new_path, cmd.data);

    char* token = strtok(new_path, "/");
    while(token != NULL){
        strcat(file_path, token);

        char* next_token = strtok(NULL, "/");
        if(next_token != NULL){
            int status = mkdir(file_path, 0755);
            if(status != 0 && errno != EEXIST){
                response.error = ERR_INVALID_PATH;
                send(sockfd, &response, sizeof(Response), 0);
                return;
            }
            strcat(file_path, "/");
        }
        else {
            if(cmd.isFIle){
                int fd = open(file_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
                if(fd <= 0){
                    close(fd);
                    response.error = ERR_PERMISSION_DENIED;
                    send(sockfd, &response, sizeof(Response), 0);
                    return;
                }
                close(fd);
            }
            else{
                int status = mkdir(file_path, 0755);
                if(status != 0 && errno != EEXIST){
                    response.error = ERR_INVALID_PATH;
                    send(sockfd, &response, sizeof(Response), 0);
                    return;
                }
            }
            response.error = ACK;
            send(sockfd, &response, sizeof(Response), 0);
            printf("Hello\n");
            return;
        }
    }
}

void deleteFolderContents(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) return;

    struct dirent* entry;
    char full_path[512];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat entry_stat;
        if (stat(full_path, &entry_stat) == 0) {
            if (S_ISDIR(entry_stat.st_mode)) {
                deleteFolderContents(full_path);
                rmdir(full_path);
            } else {
                remove(full_path);
            }
        }
    }

    closedir(dir);
}

void handle_nm_delete(int sockfd, Command cmd){
    char* path;
    path = malloc(512);
    strcpy(path, root_path);
    strcat(path, cmd.path);

    Response response;

    struct stat path_stat;
    if(stat(path, &path_stat) == 0){
        if(S_ISDIR(path_stat.st_mode)){
            deleteFolderContents(path);
            if(rmdir(path) == 0){
                response.error = ACK;
            }
            else{
                response.error = ERR_PERMISSION_DENIED;
            }
        }
        else{
            if(remove(path) == 0){
                response.error = ACK;
            }
            else{
                response.error = ERR_PERMISSION_DENIED;
            }
        }
    }
    else{
        response.error = ERR_PERMISSION_DENIED;
    }
    free(path);
    send(sockfd, &response, sizeof(Response), 0);
}

void handle_client_copy(int sockfd, Command cmd){
    printf("Client requested command: CMD_COPY for path %s\n", cmd.path);
    char final_path[512];
    strcpy(final_path, root_path);
    strcat(final_path, cmd.path);
    Response response;
    recv(sockfd, &response, sizeof(Response), 0);
    if(!strcmp(response.ip, my_info.server_details.ip) && response.port == my_info.server_details.port){
        char final_dest_path[512];
        strcpy(final_dest_path, root_path);
        if(cmd.dest_path[0] != '/')
            strcat(final_dest_path, "/");
        strcat(final_dest_path, cmd.dest_path);
        copy_path(final_path, final_dest_path);
        if(copy_path(final_path, final_dest_path) == -1){
            response.error = ERR_PERMISSION_DENIED;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
        else{
            response.error = ACK;
            send(sockfd, &response, sizeof(Response), 0);
        }
        my_info.path_count = 0;
        scan_directory(root_path);
        register_ss();
        return;
    }
    else {
        int ss2_connect = connect_to_server(response.ip, response.port);
        if(ss2_connect < 0){
            response.error = ERR_SERVER_DOWN;
            send(sockfd, &response, sizeof(Response), 0);
            return;
        }
        chdir(root_path);
        strcpy(final_path, ".");
        if(cmd.path[0] != '/')
            strcat(final_path, "/");
        strcat(final_path, cmd.path);
        char* temp_file_name;
        temp_file_name = malloc(512);
        temp_file_name = "temp.zip";
        create_zip(final_path, temp_file_name);
        cmd.type = CMD_RECEIVE_COPY;
        send(ss2_connect, &cmd, sizeof(Command), 0);
        recv(ss2_connect, &response, sizeof(Response), 0);
        if(response.error == ACK){
            send_file(ss2_connect, temp_file_name);
        }
        recv(ss2_connect, &response, sizeof(Response), 0);
        close(ss2_connect);
        remove("./temp.zip");
        chdir("../");
    }
    response.error = ACK;
    send(sockfd, &response, sizeof(Response), 0);  
}


void handle_receive_copy(int sockfd, Command cmd){
    printf("Storage Server requested command: CMD_RECEIVE_COPY for path %s\n", cmd.path);
    char final_path[512];
    Response response;
    strcpy(final_path, root_path);
    if(cmd.dest_path[0] != '/')
        strcat(final_path, "/");
    strcat(final_path, cmd.dest_path);
    printf("Final path: %s\n", final_path);    
    chdir(final_path);
    response.error = ACK;
    send(sockfd, &response, sizeof(Response), 0);
    receive_file(sockfd);
    if(unzip_and_delete_in_place("temp.zip")!= -1)
        response.error = ACK;
    else
        response.error = ERR_PERMISSION_DENIED;
    send(sockfd, &response, sizeof(Response), 0);
    chdir("../");
    my_info.path_count = 0;
    scan_directory(root_path);
    register_ss();
}

void stream_music_file(const char* filepath, int sock) {
    int file_fd;
    struct sockaddr_in server_addr;
    char buffer[MAX_BUFFER];
    ssize_t bytes_read;

    // Open file
    file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        perror("File open error");
        return;
    }

    while ((bytes_read = read(file_fd, buffer, MAX_BUFFER)) > 0) {
        if (write(sock, buffer, bytes_read) < 0) {
            perror("Send failed");
            break;
        }
    }

    close(file_fd);
}

void handle_client_stream(int sockfd, Command cmd){
    char final_path[512];
    if(cmd.isbackup == 1){
        strcpy(final_path, cmd.path);
    }
    else {
        strcpy(final_path, root_path);
        strcat(final_path, cmd.path);
    }
    stream_music_file(final_path, sockfd);
}

void handle_client_info(int sockfd, Command cmd){
    printf("Client requested command: CMD_INFO for path %s\n", cmd.path);
    char file_path[512];
    if(cmd.isbackup == 1){
        strcpy(file_path, cmd.path);
    }
    else {
        strcpy(file_path, root_path);
        strcat(file_path, cmd.path);
    }

    Response response;
    char output[MAX_BUFFER];
    int count = 0;

    struct stat filestat;
    if(stat(file_path, &filestat) < 0){
        response.error = ERR_PERMISSION_DENIED;
        send(sockfd, &response, sizeof(Response), 0);
    }

    mode_t mode = filestat.st_mode;
    output[count++] = S_ISDIR(mode) ? 'd' : '-';
    output[count++] = (mode & S_IRUSR) ? 'r' : '-';
    output[count++] = (mode & S_IWUSR) ? 'w' : '-';
    output[count++] = (mode & S_IXUSR) ? 'x' : '-';
    output[count++] = (mode & S_IRGRP) ? 'r' : '-';
    output[count++] = (mode & S_IWGRP) ? 'w' : '-';
    output[count++] = (mode & S_IXGRP) ? 'x' : '-';
    output[count++] = (mode & S_IROTH) ? 'r' : '-';
    output[count++] = (mode & S_IWOTH) ? 'w' : '-';
    output[count++] = (mode & S_IXOTH) ? 'x' : '-';
    output[count++] = '\t';
    output[count++] = '\0';

    long nlink = filestat.st_nlink;
    sprintf(output+count, "%ld\t", nlink);

    struct passwd *pw = getpwuid(filestat.st_uid);
    struct group  *gr = getgrgid(filestat.st_gid);
    const char *owner = pw ? pw->pw_name : "unknown";
    const char *group = gr ? gr->gr_name : "unknown";

    strcat(output, owner);
    strcat(output, "\t");
    strcat(output, group);
    strcat(output, "\t");

    long size = filestat.st_size;
    sprintf(output+strlen(output), "%ld\t", size);

    char time_str[20];
    strftime(time_str, 20, "%Y-%m-%d %H:%M:%S", localtime(&filestat.st_mtime));
    strcat(output, time_str);

    response.error = ACK;
    response.buffer_size = strlen(output)+1;
    send(sockfd, &response, sizeof(Response), 0);
    send(sockfd, output, strlen(output), 0);
}

void* handle_connection(void* arg){
    ThreadArgs* thread_args = (ThreadArgs*)arg;
    int client_socket = thread_args->socket;
    Command cmd = {0};
    
    if(recv(client_socket, &cmd, sizeof(Command), 0) < 0){
        close(client_socket);
        free(thread_args);
        return NULL;
    }

    if(cmd.type == CMD_BACKUP){
        handle_storage_server_backup(client_socket, cmd);
    }
    else if(cmd.type == CMD_READ){
        handle_client_read(client_socket, cmd);   
    }
    else if(cmd.type == CMD_WRITE){
        handle_client_write(client_socket, cmd);
    }
    else if(cmd.type == CMD_CREATE){
        handle_nm_create(client_socket, cmd);
    }
    else if(cmd.type == CMD_DELETE){
        handle_nm_delete(client_socket, cmd);
    }
    else if(cmd.type == CMD_STREAM){
        handle_client_stream(client_socket, cmd);
    }
    else if(cmd.type == CMD_INFO){
        handle_client_info(client_socket, cmd);
    }
    else if(cmd.type == CMD_COPY){
        handle_client_copy(client_socket, cmd);
    }
    else if(cmd.type == CMD_RECEIVE_COPY){
        handle_receive_copy(client_socket, cmd);
    }
    else if(cmd.type == CMD_RECEIVE_BACKUP){
        receive_and_save_file(client_socket, "backup.zip");
        unzip_and_delete_in_place("backup.zip");
    }
    else if(cmd.type == CMD_PING){
    }
    else{
        log_message("Invalid command type %d", cmd.type);
    }

    close(client_socket);
    free(thread_args);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        printf("Usage: %s <nm_ip> <nm_port> <client_port> <root_path>\n", argv[0]);
        return 1;
    }
    strcpy(nm_ip, argv[1]);
    nm_port = atoi(argv[2]);
    my_info.server_details.port = atoi(argv[3]);
    my_info.path_count = 0;
    strcpy(root_path, argv[4]);
    scan_directory(argv[4]);

    char hostbuffer[256];
    char *IPbuffer;
    struct hostent *host_entry;
    int hostname;
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    host_entry = gethostbyname(hostbuffer);

    IPbuffer = inet_ntoa(*((struct in_addr*)
                        host_entry->h_addr_list[0]));

    strcpy(my_info.server_details.ip, IPbuffer);

    register_ss();

    int server_socket = create_socket();
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(my_info.server_details.port)
    };

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_message("Bind failed: %s", strerror(errno));
        return 1;
    }


    listen(server_socket, SOMAXCONN);
    log_message("Storage server started on port %d", my_info.server_details.port);

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
            log_message("Failed to create thread");
            close(client_socket);
            free(thread_args);
        }

        pthread_attr_destroy(&attr);
    }

    return 0;
}