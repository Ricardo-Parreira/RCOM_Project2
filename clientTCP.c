#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdbool.h>

#define SERVER_PORT 21
#define IP_MAX_SIZE 16
#define BUFFER_SIZE 500
#define INVALID_INPUT "Usage: ftp://[<user>:<password>@]<host>/<url-path>\n"

static char filename[BUFFER_SIZE];

// Simplified input structure
typedef struct {
    char *user;
    char *password;
    char *host;
    char *url_path;
} FTPInput;

// Parse URL into user, password, host, and path
int parseUrl(char *url, FTPInput *input) {
    char *uph, *token = strtok(url, "//");
    if (strcmp(token, "ftp:") != 0) {
        printf(INVALID_INPUT);
        return -1;
    }
    uph = strtok(NULL, "/");
    input->url_path = strtok(NULL, "\0");

    if (strstr(uph, "@")) { // Includes user and password
        input->user = strtok(uph, ":");
        input->password = strtok(NULL, "@");
        input->host = strtok(NULL, "\0");
    } else { // Anonymous login
        input->user = "anonymous";
        input->password = "password";
        input->host = uph;
    }
    return 0;
}

// Extract filename from URL path
void extractFilename(const char *url_path, char *filename) {
    const char *last_slash = strrchr(url_path, '/');
    strcpy(filename, last_slash ? last_slash + 1 : url_path);
}

// Create and connect socket to server
int createSocket(const char *ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Socket connection failed");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// Send command to socket
int sendCommand(int socket_fd, const char *cmd, const char *param) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s %s\r\n", cmd, param);
    return write(socket_fd, buffer, strlen(buffer)) == (ssize_t)strlen(buffer) ? 0 : -1;
}

// Read response from socket
int readResponse(FILE *socket_stream, char *buffer, size_t buffer_size) {
    if (!fgets(buffer, buffer_size, socket_stream)) return -1;
    printf("%s", buffer);
    return atoi(buffer); // Parse response code
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Error: Not enough arguments.\n");
        return -1;
    }

    FTPInput input;
    if (parseUrl(argv[1], &input) < 0) return -1;
    extractFilename(input.url_path, filename);

    struct hostent *host = gethostbyname(input.host);
    if (!host) {
        herror("DNS resolution failed");
        return -1;
    }
    char ip[IP_MAX_SIZE];
    strcpy(ip, inet_ntoa(*(struct in_addr *)host->h_addr));

    int control_fd = createSocket(ip, SERVER_PORT);
    if (control_fd < 0) return -1;
    FILE *control_stream = fdopen(control_fd, "r+");

    char buffer[BUFFER_SIZE];
    if (readResponse(control_stream, buffer, sizeof(buffer)) != 220) return -1;

    if (sendCommand(control_fd, "USER", input.user) < 0 || 
        readResponse(control_stream, buffer, sizeof(buffer)) != 331 ||
        sendCommand(control_fd, "PASS", input.password) < 0 ||
        readResponse(control_stream, buffer, sizeof(buffer)) != 230) return -1;

    if (sendCommand(control_fd, "PASV", "") < 0 ||
        readResponse(control_stream, buffer, sizeof(buffer)) != 227) return -1;

    int a, b, c, d, p1, p2;
    sscanf(strchr(buffer, '('), "(%d,%d,%d,%d,%d,%d)", &a, &b, &c, &d, &p1, &p2);
    int data_port = p1 * 256 + p2;

    int data_fd = createSocket(ip, data_port);
    if (data_fd < 0) return -1;
    FILE *data_stream = fdopen(data_fd, "r");

    if (sendCommand(control_fd, "RETR", input.url_path) < 0 ||
        readResponse(control_stream, buffer, sizeof(buffer)) != 150) return -1;

    FILE *file = fopen(filename, "wb");
    while (!feof(data_stream)) {
        size_t bytes = fread(buffer, 1, sizeof(buffer), data_stream);
        fwrite(buffer, 1, bytes, file);
    }
    fclose(file);
    fclose(data_stream);
    close(data_fd);

    if (readResponse(control_stream, buffer, sizeof(buffer)) != 226) return -1;

    fclose(control_stream);
    close(control_fd);
    return 0;
}
