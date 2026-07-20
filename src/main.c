#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

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
        close(socket_fd);
        close(client_fd);
        return 1;
    }
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Received %d bytes:\n%s\n", bytes_received, buffer);
    } else {
        printf("No bytes received, skipping HTTP response.\n");
        close(client_fd);
        close(socket_fd);
        return 0;
    }

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
        close(socket_fd);
        return 1;
    }

    printf("Parsed request line: %s\n", parse);
    char method[16], path[256], version[16];
    if (sscanf(parse, "%15s %255s %15s", method, path, version) != 3) {
        printf("Invalid HTTP request line: %s\n", parse);
        close(client_fd);
        close(socket_fd);
        return 1;
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
        close(socket_fd);
        return 1;
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
        close(socket_fd);
        return 1;
    }


    if (strlen(method) == 0 || strlen(path) == 0 || strlen(version) == 0) {
        printf("Invalid HTTP request line: %s\n", parse);
        close(client_fd);
        close(socket_fd);
        return 1;
    }


    printf("Parsed method: %s\n", method);
    printf("Parsed path: %s\n", path);
    printf("Parsed version: %s\n", version);

    const char *body = "<html><body><h1>Hello from C HTTP server</h1></body></html>";
    char response[1024];
    int response_len = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 200 OK\r\n"
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
            close(socket_fd);
            close(client_fd);
            return 1;
        }
    }

    close(client_fd);
    close(socket_fd);
    return 0;
}
