#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>

extern int connectToServer_TCP(const char *hostname, int port);
extern char *resolveHostnameToIP(const char *hostname);

#define FTP_PORT 21
#define BUFFER_SIZE 1024
#define MAX_CREDENTIAL_SIZE 512

void connectToServer(const char *hostname, int *controlSock);
void sendCommand(int sock, const char *cmd);
void receiveResponse(int sock, char *response, size_t size);
void authenticate(int controlSock);
void parsePASVResponse(const char *response, char *dataIP, int *dataPort);
void downloadFile(int controlSock, const char *filename);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <hostname> <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *hostname = argv[1];
    const char *filename = argv[2];

    char *ip = resolveHostnameToIP(hostname);
    if (!ip) {
        fprintf(stderr, "Error resolving hostname %s.\n", hostname);
        exit(EXIT_FAILURE);
    }
    printf("Resolved IP address: %s\n", ip);

    int controlSock;
    connectToServer(hostname, &controlSock);

    char response[BUFFER_SIZE];
    receiveResponse(controlSock, response, sizeof(response));
    printf("Server response: %s\n", response);

    authenticate(controlSock);
    downloadFile(controlSock, filename);

    close(controlSock);
    printf("Connection closed.\n");

    return 0;
}

void connectToServer(const char *hostname, int *controlSock) {
    char *ip = resolveHostnameToIP(hostname);
    if (!ip) {
        fprintf(stderr, "Error resolving hostname %s.\n", hostname);
        exit(EXIT_FAILURE);
    }
    printf("Resolved IP address: %s\n", ip);

    *controlSock = connectToServer_TCP(ip, FTP_PORT);
    if (*controlSock < 0) {
        fprintf(stderr, "Error: Could not connect to server %s on port %d\n", hostname, FTP_PORT);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s on port %d\n", hostname, FTP_PORT);
}

void sendCommand(int sock, const char *cmd) {
    if (send(sock, cmd, strlen(cmd), 0) < 0) {
        perror("Error sending command");
        exit(EXIT_FAILURE);
    }
    printf("Command sent: %s", cmd);
}

void receiveResponse(int sock, char *response, size_t size) {
    memset(response, 0, size);
    if (recv(sock, response, size - 1, 0) < 0) {
        perror("Error receiving response");
        exit(EXIT_FAILURE);
    }
    printf("Response received: %s", response);
}

void authenticate(int controlSock) {
    char username[MAX_CREDENTIAL_SIZE];
    char password[MAX_CREDENTIAL_SIZE];
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    // Solicitar o username
    printf("Enter username: ");
    if (!fgets(username, sizeof(username), stdin)) {
        fprintf(stderr, "Error reading username.\n");
        exit(EXIT_FAILURE);
    }
    username[strcspn(username, "\n")] = '\0'; // Remover newline

    // Enviar comando USER
    snprintf(command, sizeof(command), "USER %s\r\n", username);
    sendCommand(controlSock, command);

    // Ignorar múltiplas respostas informacionais `220`
    do {
        receiveResponse(controlSock, response, sizeof(response));
        if (strncmp(response, "220", 3) == 0) {
            printf("Info: Additional 220 response ignored.\n");
        }
    } while (strncmp(response, "220", 3) == 0);

    // Verificar se o servidor pede senha ou realiza login
    if (strncmp(response, "331", 3) == 0) {
        // Solicitar a senha
        printf("Enter password: ");
        if (!fgets(password, sizeof(password), stdin)) {
            fprintf(stderr, "Error reading password.\n");
            exit(EXIT_FAILURE);
        }
        password[strcspn(password, "\n")] = '\0'; // Remover newline

        // Enviar comando PASS
        snprintf(command, sizeof(command), "PASS %s\r\n", password);
        sendCommand(controlSock, command);
        receiveResponse(controlSock, response, sizeof(response));

        if (strncmp(response, "230", 3) != 0) {
            fprintf(stderr, "Login failed: %s\n", response);
            exit(EXIT_FAILURE);
        }
        printf("Login successful.\n");

    } else if (strncmp(response, "230", 3) == 0) {
        // Login direto, sem necessidade de senha
        printf("Login successful (no password required).\n");

    } else {
        // Resposta inesperada
        fprintf(stderr, "Unexpected response after USER: %s\n", response);
        exit(EXIT_FAILURE);
    }
}


void parsePASVResponse(const char *response, char *dataIP, int *dataPort) {
    int ip1, ip2, ip3, ip4, p1, p2;
    if (sscanf(response, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
               &ip1, &ip2, &ip3, &ip4, &p1, &p2) != 6) {
        fprintf(stderr, "Error parsing PASV response: %s\n", response);
        exit(EXIT_FAILURE);
    }
    snprintf(dataIP, BUFFER_SIZE, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
    *dataPort = p1 * 256 + p2;
}

void downloadFile(int controlSock, const char *filename) {
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char dataIP[BUFFER_SIZE];
    int dataPort;

    // Send PASV command
    snprintf(command, sizeof(command), "PASV\r\n");
    sendCommand(controlSock, command);
    receiveResponse(controlSock, response, sizeof(response));

    if (strncmp(response, "227", 3) != 0) {
        fprintf(stderr, "Error: Failed to enter passive mode. Server response: %s\n", response);
        exit(EXIT_FAILURE);
    }
    parsePASVResponse(response, dataIP, &dataPort);
    printf("Passive mode: IP = %s, Port = %d\n", dataIP, dataPort);

    // Connect to the data socket
    int dataSock = connectToServer_TCP(dataIP, dataPort);
    if (dataSock < 0) {
        fprintf(stderr, "Error: Could not connect to data socket at %s:%d\n", dataIP, dataPort);
        exit(EXIT_FAILURE);
    }

    // Extract the base filename (strip directories)
    const char *baseFilename = basename((char *)filename);

    // Open file for writing
    FILE *file = fopen(baseFilename, "wb");
    if (!file) {
        fprintf(stderr, "Error opening file '%s' for writing: %s\n", baseFilename, strerror(errno));
        close(dataSock);
        return;
    }

    // Send RETR command
    snprintf(command, sizeof(command), "RETR %s\r\n", filename);
    sendCommand(controlSock, command);
    receiveResponse(controlSock, response, sizeof(response));

    if (strncmp(response, "150", 3) != 0) {
        fprintf(stderr, "Error: File transfer failed. Server response: %s\n", response);
        fclose(file);
        close(dataSock);
        return;
    }

    // Transfer data from socket to file
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    while ((bytesRead = recv(dataSock, buffer, sizeof(buffer), 0)) > 0) {
        if (fwrite(buffer, 1, bytesRead, file) != (size_t)bytesRead) {
            fprintf(stderr, "Error writing to file '%s': %s\n", baseFilename, strerror(errno));
            fclose(file);
            close(dataSock);
            return;
        }
    }

    if (bytesRead < 0) {
        fprintf(stderr, "Error receiving data: %s\n", strerror(errno));
    }

    fclose(file);
    close(dataSock);

    // Check final server response
    receiveResponse(controlSock, response, sizeof(response));
    if (strncmp(response, "226", 3) != 0) {
        fprintf(stderr, "Error: File transfer not completed. Server response: %s\n", response);
    } else {
        printf("File transfer complete. File saved as: %s\n", baseFilename);
    }
}