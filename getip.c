#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <arpa/inet.h>

char *resolveHostnameToIP(const char *hostname) {
    struct hostent *host;
    static char ip[INET_ADDRSTRLEN];

    // Resolver hostname para endereço IP
    host = gethostbyname(hostname);
    if (!host) {
        fprintf(stderr, "Error resolving hostname: %s\n", hstrerror(h_errno));
        return NULL;
    }

    // Converter o endereço IP para string
    if (!inet_ntop(AF_INET, host->h_addr_list[0], ip, INET_ADDRSTRLEN)) {
        perror("Error converting address to string");
        return NULL;
    }

    return ip;
}
