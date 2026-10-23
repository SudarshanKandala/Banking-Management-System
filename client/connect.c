#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../include/config.h"
#include "connect.h"

int client_connect(){
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Create TCP Socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(sock_fd == -1) {
        perror("Error creating socket from client\n");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if(inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) != -1){
        printf("Invalid server address\n");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if(connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
        printf("Error connecting to server\n");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    // printf("Connected to server...\n");
    return sock_fd;
}