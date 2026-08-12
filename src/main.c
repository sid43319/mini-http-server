#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>

int send_all(int client_fd, char *buffer, size_t bytes);
void error_handling(int client_fd, char *absolute_file_path, char *error_type, char *body);

int main(void) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int result = getaddrinfo(NULL, "8080", &hints, &res);
    if (result != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
        return 1;
    }
    int socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_fd == -1) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }
    int bind_result = bind(socket_fd, res->ai_addr, res->ai_addrlen);
    if (bind_result == -1) {
        perror("bind");
        freeaddrinfo(res);
        close(socket_fd);
        return 1;
    }
    int listen_result = listen(socket_fd, 10);
    if (listen_result == -1) {
        perror("listen");
        freeaddrinfo(res);
        close(socket_fd);
        return 1;
    }
    freeaddrinfo(res);

    // Resolve the absolute path of the webroot once, at startup
    char *absolute_path = realpath("./www", NULL);
    if (absolute_path == NULL) {
        perror("realpath");
        close(socket_fd);
        return 1;
    }

    while (1) {
        int client_fd = accept(socket_fd, NULL, NULL);
        if (client_fd == -1) {
            perror("accept");
            close(socket_fd);
            return 1;
        }
        char buffer[1024];
        int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received == -1) {
            perror("recv");
            close(client_fd);
            continue;
        }
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("Received %d bytes:\n%s\n", bytes_received, buffer);
        } else {
            printf("No bytes received, skipping HTTP response.\n");
            close(client_fd);
            continue;
        }

        // Parse the request line
        char parse[1024];
        int found_end = 0;
        for (int i = 0; i < bytes_received - 1; i++) {
            if (buffer[i] == '\r' && buffer[i + 1] == '\n') {
                size_t line_len = (size_t)i;
                if (line_len >= sizeof(parse)) {
                    line_len = sizeof(parse) - 1;
                }
                memcpy(parse, buffer, line_len);
                parse[line_len] = '\0';
                found_end = 1;
                break;
            }
        }

        if (!found_end) {
            printf("Invalid HTTP request: no CRLF found\n");
            close(client_fd);
            continue;
        }

        printf("Parsed request line: %s\n", parse);
        char method[16], path[256], version[16];

        int first_space_index = -1;
        int parse_len = (int)strlen(parse);

        for (int i = 0; i < parse_len; i++) {
            if (parse[i] == ' ') {
                size_t method_len = (size_t)(i);
                if (method_len >= sizeof(method)) {
                    method_len = sizeof(method) - 1;
                }
                memcpy(method, parse, method_len);
                method[method_len] = '\0';
                first_space_index = i;
                break;
            }
        }

        if (first_space_index == -1) {
            printf("Invalid HTTP request line: %s\n", parse);
            close(client_fd);
            continue;
        }

        int second_space_index = -1;

        for (int i = first_space_index + 1; i < parse_len; i++) {
            if (parse[i] == ' ') {
                second_space_index = i;
                size_t path_len = second_space_index - first_space_index - 1;
                if (path_len >= sizeof(path)) {
                    path_len = sizeof(path) - 1;
                }
                memcpy(path, parse + first_space_index + 1, path_len);
                size_t version_len = parse_len - second_space_index - 1;
                if (version_len >= sizeof(version)) {
                    version_len = sizeof(version) - 1;
                }
                memcpy(version, parse + second_space_index + 1, version_len);
                path[path_len] = '\0';
                version[version_len] = '\0';
                break;
            }
        }

        if (second_space_index == -1) {
            printf("Invalid HTTP request line: %s\n", parse);
            close(client_fd);
            continue;
        }

        if (strlen(method) == 0 || strlen(path) == 0 || strlen(version) == 0) {
            printf("Invalid HTTP request line: %s\n", parse);
            close(client_fd);
            continue;
        }

        // Validation

        if (strcmp(method, "GET") != 0) {
            error_handling(client_fd, NULL, "405 Method Not Allowed", "<html><body><h1>405 Method Not Allowed</h1></body></html>");
            continue;
        }

        if (path[0] != '/') {
            error_handling(client_fd, NULL, "400 Bad Request", "<html><body><h1>400 Bad Request</h1></body></html>");
            continue;
        }

        if (strcmp(version, "HTTP/1.1") != 0) {
            error_handling(client_fd, NULL, "505 HTTP Version Not Supported", "<html><body><h1>505 HTTP Version Not Supported</h1></body></html>");
            continue;
        }

        printf("Parsed method: %s\n", method);
        printf("Parsed path: %s\n", path);
        printf("Parsed version: %s\n", version);

        // Build candidate path: webroot + request path
        char file_path[256] = "./www";
        size_t current_len = strlen(file_path);
        int rtn;
        size_t remaining_length;
        if (strcmp(path, "/") == 0) {
            rtn = snprintf(file_path, sizeof(file_path), "./www/index.html");
            remaining_length = sizeof(file_path);
        } else {
            rtn = snprintf(file_path + current_len, sizeof(file_path) - current_len, "%s", path);
            remaining_length = sizeof(file_path) - current_len;
        }

        if (rtn < 0) {
            printf("Formatting error");
            close(client_fd);
            continue;
        } else if ((size_t)rtn >= remaining_length) {
            printf("Not enough room, output was truncated.");
            close(client_fd);
            continue;
        }

        // Resolve the candidate path
        char *absolute_file_path = realpath(file_path, NULL);
        if (absolute_file_path == NULL) {
            error_handling(client_fd, NULL, "404 Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
            continue;
        }

        // Verify resolved path is inside the webroot
        size_t absolute_path_len = strlen(absolute_path);
        size_t absolute_file_path_len = strlen(absolute_file_path);

        if (absolute_file_path_len < absolute_path_len) {
            error_handling(client_fd, absolute_file_path, "403 Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
            continue;
        }

        bool is_valid_path = true;
        for (size_t i = 0; i < absolute_path_len; i++) {
            if (absolute_path[i] != absolute_file_path[i]) {
                is_valid_path = false;
                break;
            }
        }

        if (!is_valid_path) {
            error_handling(client_fd, absolute_file_path, "403 Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
            continue;
        }

        if (absolute_file_path[absolute_path_len] != '/' && absolute_file_path[absolute_path_len] != '\0') {
            error_handling(client_fd, absolute_file_path, "403 Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
            continue;
        }

        // Verify it's a regular file
        struct stat file_stat;
        int stat_result = stat(absolute_file_path, &file_stat);

        if (stat_result < 0) {
            error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
            continue;
        }

        if (!S_ISREG(file_stat.st_mode)) {
            error_handling(client_fd, absolute_file_path, "404 Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
            continue;
        }

        // Open and serve the resolved file
        FILE *file = fopen(absolute_file_path, "r");
        if (file == NULL) {
            error_handling(client_fd, absolute_file_path, "404 Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
            continue;
        }

        int fseek_result = fseek(file, 0, SEEK_END);
        if (fseek_result != 0) {
            error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
            fclose(file);
            continue;
        }
        long file_size = ftell(file);
        if (file_size < 0) {
            error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
            fclose(file);
            continue;
        }
        size_t file_size_t = (size_t)file_size;
        rewind(file);
        char *file_buffer = malloc(file_size_t + 1);
        if (file_buffer == NULL) {
            error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
            fclose(file);
            continue;
        }
        size_t bytes_read = fread(file_buffer, 1, file_size_t, file);
        file_buffer[bytes_read] = '\0';
        if (bytes_read != file_size_t) {
            if (ferror(file)) {
                error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
            } else {
                error_handling(client_fd, absolute_file_path, "404 Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
            }
            free(file_buffer);
            fclose(file);
            continue;
        }

        // find last occurrence of '.' in the absolute file path
        // to get the file extension

        char *file_extension = strrchr(absolute_file_path, '.');
        if (file_extension == NULL) {
            file_extension = "";
        } else {
            file_extension++;
        }
        char *content_type = "text/plain";
        if (strcmp(file_extension, "html") == 0) {
            content_type = "text/html";
            printf("Serving HTML file: %s\n", absolute_file_path);
        } else if (strcmp(file_extension, "css") == 0) {
            content_type = "text/css";
            printf("Serving CSS file: %s\n", absolute_file_path);
        } else if (strcmp(file_extension, "js") == 0) {
            content_type = "application/javascript";
            printf("Serving JS file: %s\n", absolute_file_path);
        } else if (strcmp(file_extension, "png") == 0) {
            content_type = "image/png";
            printf("Serving PNG file: %s\n", absolute_file_path);
        } else if (strcmp(file_extension, "jpg") == 0 || strcmp(file_extension, "jpeg") == 0) {
            content_type = "image/jpeg";
            printf("Serving JPG file: %s\n", absolute_file_path);
        } else if (strcmp(file_extension, "gif") == 0) {
            content_type = "image/gif";
            printf("Serving GIF file: %s\n", absolute_file_path);
        } else {
            printf("Serving unknown file type: %s\n", absolute_file_path);
            content_type = "application/octet-stream";
        }

        char response[1024];
        int response_len = snprintf(
            response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            content_type,
            bytes_read
        );

        if (response_len > 0 && response_len < (int)sizeof(response)) {
            int send_fd = send_all(client_fd, response, (size_t)response_len);
            if (send_fd == 0) {
                error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
                free(file_buffer);
                fclose(file);
                continue;
            } else if (send_fd == 2) {
                error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
                free(file_buffer);
                fclose(file);
                continue;
            }

            int send_file_fd = send_all(client_fd, file_buffer, bytes_read);
            if (send_file_fd == 0) {
                error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
                free(file_buffer);
                fclose(file);
                continue;
            } else if (send_file_fd == 2) {
                error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
                free(file_buffer);
                fclose(file);
                continue;
            }
        } else {
            printf("Invalid response");
            error_handling(client_fd, absolute_file_path, "500 Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
            free(file_buffer);
            fclose(file);
            continue;
        }

        free(file_buffer);
        fclose(file);
        continue;
    }

    return 0;
}

//method for sending bytes to the client, returns 1 if successful, 0 if error, 2 if zero progress

int send_all(int client_fd, char *buffer, size_t bytes) {
    size_t total_sent = 0;

    while (total_sent < bytes) {
        int send_fd = send(client_fd, buffer + total_sent, bytes - total_sent, 0);
        if (send_fd == -1) {
            return 0;
        }
        if (send_fd == 0) {
            return 2;
        }
        total_sent += send_fd;
    }

    return 1;
}

//handles all http errors, sends the appropriate response to the client and closes the connection

void error_handling(int client_fd, char *absolute_file_path, char *error_type, char *body) {
    char response[1024];
    int response_len = snprintf(
        response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        error_type, strlen(body), body
    );
    if (response_len > 0 && response_len < (int)sizeof(response)) {
        int send_fd = send_all(client_fd, response, (size_t)response_len);
            if (send_fd == 0) {
                perror("send");
            }
    }
    if (absolute_file_path != NULL) {
        free(absolute_file_path);
    }
    close(client_fd);

}


