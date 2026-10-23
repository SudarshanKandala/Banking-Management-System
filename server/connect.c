#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/config.h"
#include "connect.h"

void server_start(client_handler_fn handle_client)
{
    int sd;
    struct sockaddr_in serv;

    if (handle_client == NULL) {
        fprintf(stderr, "Client handler is NULL\n");
        exit(EXIT_FAILURE);
    }

    // Automatically reap terminated child processes.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDWAIT;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // 1. Create TCP socket.
    sd = socket(AF_INET, SOCK_STREAM, 0);

    if (sd == -1) {
        perror("socket creation at server failed");
        exit(EXIT_FAILURE);
    }

    // Allow the server to reuse the address after restarting.
    int reuse_addr = 1;

    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_addr, sizeof(reuse_addr)) == -1) {
        perror("setsockopt");
        close(sd);
        exit(EXIT_FAILURE);
    }

    // 2. Configure server address.
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_port = htons(SERVER_PORT);

    // Bind specifically to 127.0.0.1 for your local demo.
    if (inet_pton(AF_INET, SERVER_IP, &serv.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP: %s\n", SERVER_IP);
        close(sd);
        exit(EXIT_FAILURE);
    }

    // 3. Bind socket to address and port.
    if (bind(sd, (struct sockaddr *)&serv,
             sizeof(serv)) == -1) {
        perror("bind");
        close(sd);
        exit(EXIT_FAILURE);
    }

    // 4. Start listening for connections.
    if (listen(sd, 5) == -1) {
        perror("listen");
        close(sd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on %s:%d\n",
           SERVER_IP, SERVER_PORT);

    fflush(stdout);

    // 5. Accept and service multiple clients.
    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);

        int nsd = accept(
            sd,
            (struct sockaddr *)&cli,
            &cli_len
        );

        if (nsd == -1) {
            if (errno == EINTR)
                continue;

            perror("socket accept");
            continue;
        }

        // 6. Create one child process for this client.
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            close(nsd);
            continue;
        }

        if (pid == 0) {
            // CHILD PROCESS

            // Child doesn't need the listening socket.
            close(sd);

            // Process requests from this client.
            handle_client(nsd);

            // Close the client connection.
            close(nsd);

            _exit(EXIT_SUCCESS);
        }

        // PARENT PROCESS

        // Parent doesn't handle this client's requests.
        close(nsd);

        // Return to accept() to wait for another client.
    }
}