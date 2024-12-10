#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

int connectToServer_TCP(const char *hostname, int port) {
    int sockfd;
    struct sockaddr_in serverAddr;

    // Criar socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        return -1;
    }

    // Configurar endereço do servidor
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    // Verificar se hostname já é um endereço IP
    if (inet_pton(AF_INET, hostname, &serverAddr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid IP address or hostname: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Conectar ao servidor
    if (connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Error connecting to server");
        close(sockfd);
        return -1;
    }

    return sockfd;
}
