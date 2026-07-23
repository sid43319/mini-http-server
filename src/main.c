#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
int send_all (int client_fd, char* buffer, size_t bytes);
int main(void) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // Allow IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP socket
    hints.ai_flags = AI_PASSIVE; // For wildcard IP address
    
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

    while (1) {
        int client_fd = accept(socket_fd, NULL, NULL);
        if (client_fd == -1) {
            perror("accept");
            close(socket_fd);
            return 1;
        }
        char buffer[1024];
        //recv doesnt need null terminator since it takes in length of bytes
        int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        //buffer needs to be null terminated to be printed as a string
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

        //Parsing part of HTTP server

        // Parse the request line from the buffer.
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
        if (sscanf(parse, "%15s %255s %15s", method, path, version) != 3) {
            printf("Invalid HTTP request line: %s\n", parse);
            close(client_fd);
            continue;
        }

        //Now we need to parse method, path, and version seperately from parse
        //We do this by finding first space, then memcpy from start to index (method)
        //Then we find second space, and memcpy from index after first space to index of second space (path)
        //Finally, we use the seond space and memcpy from index after second space to end of parse string (version)

        int first_space_index = -1;
        int parse_len = (int)strlen(parse);

        for (int i = 0; i < parse_len; i++) {
            if (parse[i] == ' ') {
                size_t method_len = (size_t)(i); //returns index in size_t type
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
                //Path starts the index after the first space
                //Ends at the index of the second space
                memcpy(path, parse + first_space_index + 1, path_len);
                size_t version_len = parse_len - second_space_index - 1;
                    //for the version, we want to copy from index after second space
                    //to end of parse string
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

        //Validation parts of the request line

        if (strcmp(method, "GET") != 0) {
            const char *body = "<html><body><h1>405 Method Not Allowed</h1></body></html>";
            char response[1024];
            int response_len = snprintf(
                response,
                sizeof(response),
                "HTTP/1.1 405 Method Not Allowed\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                strlen(body),
                body
            );
            if (response_len > 0 && response_len < (int)sizeof(response)) {

                int send_fd = send(client_fd, response, (size_t)response_len, 0);
                if (send_fd == -1) {
                    perror("send");
                    close(client_fd);
                    continue;
                }
            }
            printf("405 Method Not Allowed: %s\n", method);
            close(client_fd);
            continue;
        }

        if (path[0] != '/') {
            const char *body = "<html><body><h1>400 Bad Request</h1></body></html>";
            char response[1024];
            int response_len = snprintf(
                response,
                sizeof(response),
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                strlen(body),
                body
            );
            printf("400 Bad Request: %s\n", path);
            if (response_len > 0 && response_len < (int)sizeof(response)) {

                int send_fd = send(client_fd, response, (size_t)response_len, 0);
                if (send_fd == -1) {
                    perror("send");
                    close(client_fd);
                    continue;
                }
            }
        
            close(client_fd);
            continue;
        }

        if (strcmp(version, "HTTP/1.1") != 0) {
            const char *body = "<html><body><h1>505 HTTP Version Not Supported</h1></body></html>";
            char response[1024];
            int response_len = snprintf(
                response,
                sizeof(response),
                "HTTP/1.1 505 HTTP Version Not Supported\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                strlen(body),
                body
            );
            if (response_len > 0 && response_len < (int)sizeof(response)) {

                int send_fd = send(client_fd, response, (size_t)response_len, 0);
                if (send_fd == -1) {
                    perror("send");
                    close(client_fd);
                    continue;
                }
            }
            printf("505 HTTP Version Not Supported: %s\n", version);
            close(client_fd);
            continue;
        }




        printf("Parsed method: %s\n", method);
        printf("Parsed path: %s\n", path);
        printf("Parsed version: %s\n", version);

        char file_path[256];
        if (strcmp(path, "/") == 0) {
            strncpy(file_path, "./www/index.html", sizeof(file_path) - 1);
            file_path[sizeof(file_path) - 1] = '\0';
        } else if (strcmp(path, "/about.html") == 0) {
            strncpy(file_path, "./www/about.html", sizeof(file_path) - 1);
            file_path[sizeof(file_path) - 1] = '\0';
        } else {
            const char *body = "<html><body><h1>404 Not Found</h1></body></html>";
            char response[1024];
            int response_len = snprintf(
                response,
                sizeof(response),
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                strlen(body),
                body
            );
            if (response_len > 0 && response_len < (int)sizeof(response)) {

                int send_fd = send(client_fd, response, (size_t)response_len, 0);
                if (send_fd == -1) {
                    perror("send");
                }
            }
            printf("404 Not Found: %s\n", path);
            close(client_fd);
            continue;
        }

        FILE *file = fopen(file_path, "r");
        

        if (file == NULL) {
            const char *body = "<html><body><h1>404 Not Found</h1></body></html>";
            char response[1024];
            int response_len = snprintf(
                response,
                sizeof(response),
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                strlen(body),
                body
            );
            if (response_len > 0 && response_len < (int)sizeof(response)) {

                int send_fd = send(client_fd, response, (size_t)response_len, 0);
                if (send_fd == -1) {
                    perror("send");
                }
            }
            printf("404 Not Found: %s\n", file_path);
            close(client_fd);
            continue;
        } else {

            int fseek_result = fseek(file, 0, SEEK_END);
            if (fseek_result != 0) {
                perror("fseek");
                fclose(file);
                close(client_fd);
                continue;
            }
            
            long file_size = ftell(file);

            if (file_size < 0) {
                perror("ftell");
                fclose(file);
                close(client_fd);
                continue;
            }

            size_t file_size_t = (size_t)file_size;

            rewind(file);
            char *file_buffer = malloc(file_size_t + 1);
            if (file_buffer == NULL) {
                perror("malloc");
                fclose(file);
                close(client_fd);
                continue;
            }

            size_t bytes_read = fread(file_buffer, 1, (size_t)file_size, file);
            file_buffer[bytes_read] = '\0';
            if (bytes_read != (size_t)file_size) {
                if (bytes_read < (size_t)file_size) {
                    fprintf(stderr, "fread: unexpected end of file\n");
                } else {
                    perror("fread");
                }
                free(file_buffer);
                fclose(file);
                close(client_fd);
                continue;
            }

            char response[1024];
            int response_len = snprintf(
                response,
                sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n",
                bytes_read
            );

            if (response_len > 0 && response_len < (int)sizeof(response)) {
            int send_fd = send_all(client_fd, response, (size_t)response_len);
                if (send_fd == 0) {
                    perror("send");
                    free(file_buffer);
                    fclose(file);
                    close(client_fd);
                    continue;
                } else if (send_fd == 2) {
                    printf("Zero progress");
                    free(file_buffer);
                    fclose(file);
                    close(client_fd);
                    continue;
                }

                int send_file_fd = send_all(client_fd, file_buffer, bytes_read);
                if (send_file_fd == 0) {
                    perror("send");
                    free(file_buffer);
                    fclose(file);
                    close(client_fd);
                    continue;
                } else if (send_file_fd == 2) {
                    printf("Zero progress");
                    free(file_buffer);
                    fclose(file);
                    close(client_fd);
                    continue;
                }
            } else {
                printf("Invalid response");
                free(file_buffer);
                fclose(file);
                close(client_fd);
                continue;
            }

            

            

            

        

            free(file_buffer);
            fclose(file);
            close(client_fd);
        }
    }
}


    // const char *body = "<html><body><h1>Hello from C HTTP server</h1></body></html>";
    // char response[1024];
    // int response_len = snprintf(
    //     response,
    //     sizeof(response),
    //     "HTTP/1.1 200 OK\r\n"
    //     "Content-Type: text/html; charset=utf-8\r\n"
    //     "Content-Length: %zu\r\n"
    //     "Connection: close\r\n"
    //     "\r\n"
    //     "%s",
    //     strlen(body),
    //     body
    // );

    // if (response_len > 0 && response_len < (int)sizeof(response)) {

    //     int send_fd = send(client_fd, response, (size_t)response_len, 0);
    //     if (send_fd == -1) {
    //         perror("send");
    //         close(socket_fd);
    //         close(client_fd);
    //         return 1;
    //     }
    // }

    // close(client_fd);
    // close(socket_fd);
    // return 0;


int send_all (int client_fd, char* buffer, size_t bytes) {
    size_t total_sent = 0;

    while(total_sent < bytes) {
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
