#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

int main() {
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // Allow IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP socket
    hints.ai_flags = AI_PASSIVE; // For wildcard IP address
    
    int result = getaddrinfo(NULL, "8080", &hints, &res);
    if (result != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
        freeaddrinfo(res);
        return 1;
    }
    int socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_fd == -1) {
        perror("socket");
        freeaddrinfo(res);
        close(socket_fd);
        return 1;
    }
    int result_bind = bind(socket_fd, res->ai_addr, res->ai_addrlen);
    if (result_bind == -1) {
        perror("bind");
        freeaddrinfo(res);
        close(socket_fd);
        return 1;
    }
    int result_listen = listen(socket_fd, 10);
    if (result_listen == -1) {
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
        close(client_fd);
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

    if (response_len > 0) {

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
