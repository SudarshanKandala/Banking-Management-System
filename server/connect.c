#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/config.h"
#include "connect.h"

typedef struct {
    int client_fd;
    client_handler_fn handle_client;
} ClientThreadArgs;

static void *client_thread(void *arg)
{
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    int client_fd = args->client_fd;
    client_handler_fn handle_client = args->handle_client;
    free(args);
    handle_client(client_fd);
    close(client_fd);
    return NULL;
}

void server_start(client_handler_fn handle_client)
{
    int sd;
    int reuse_addr = 1;
    struct sockaddr_in serv;
    pthread_attr_t thread_attr;

    if (handle_client == NULL) {
        fprintf(stderr, "Client handler is NULL\n");
        return;
    }

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("socket creation at server failed");
        return;
    }

    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) == -1) {
        perror("setsockopt");
        close(sd);
        return;
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serv.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP: %s\n", SERVER_IP);
        close(sd);
        return;
    }

    if (bind(sd, (struct sockaddr *)&serv, sizeof(serv)) == -1) {
        perror("bind");
        close(sd);
        return;
    }

    if (listen(sd, 5) == -1) {
        perror("listen");
        close(sd);
        return;
    }

    if (pthread_attr_init(&thread_attr) != 0) {
        fprintf(stderr, "pthread_attr_init failed\n");
        close(sd);
        return;
    }

    if (pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED) != 0) {
        fprintf(stderr, "pthread_attr_setdetachstate failed\n");
        pthread_attr_destroy(&thread_attr);
        close(sd);
        return;
    }

    printf("Server listening on %s:%d\n", SERVER_IP, SERVER_PORT);

    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int nsd = accept(sd, (struct sockaddr *)&cli, &cli_len);

        if (nsd == -1) {
            if (errno == EINTR) continue;
            perror("socket accept");
            continue;
        }

        ClientThreadArgs *args = malloc(sizeof(*args));
        if (args == NULL) {
            perror("malloc for client thread");
            close(nsd);
            continue;
        }

        args->client_fd = nsd;
        args->handle_client = handle_client;

        pthread_t thread;
        int thread_result = pthread_create(&thread, &thread_attr, client_thread, args);
        if (thread_result != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(thread_result));
            close(nsd);
            free(args);
            continue;
        }
    }

    pthread_attr_destroy(&thread_attr);
    close(sd);
}
