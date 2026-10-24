#define _POSIX_C_SOURCE 200809L
#include "connect.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/config.h"

typedef struct ClientThreadContext {
    int client_fd;
    client_handler_fn handler;
    struct ClientThreadContext *next;
} ClientThreadContext;

static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t clients_condition = PTHREAD_COND_INITIALIZER;
static ClientThreadContext *clients = NULL;
static int active_clients = 0;
static volatile sig_atomic_t stop_requested = 0;
static int listening_socket = -1;

/* A signal handler only requests shutdown and closes the listening socket. */
static void stop_server_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
    if (listening_socket != -1) close(listening_socket);
    listening_socket = -1;
}

/* Remove a finished client context from the tracked list. */
static void remove_client_locked(ClientThreadContext *context)
{
    ClientThreadContext **cursor = &clients;
    while (*cursor && *cursor != context) cursor = &(*cursor)->next;
    if (*cursor == context) *cursor = context->next;
    active_clients--;
    if (active_clients <= 0) pthread_cond_broadcast(&clients_condition);
}

/* Each accepted client gets one detached pthread and a private socket descriptor. */
static void *client_thread(void *argument)
{
    ClientThreadContext *context = argument;
    int client_fd = context->client_fd;
    /* Main handles all reads/writes for this socket until logout or disconnect. */
    context->handler(client_fd);
    pthread_mutex_lock(&clients_mutex);
    close(client_fd);
    remove_client_locked(context);
    pthread_mutex_unlock(&clients_mutex);
    free(context);
    return NULL;
}

/* Start TCP listening and serve concurrent clients until SIGINT/SIGTERM. */
void server_start(client_handler_fn handle_client_callback)
{
    if (!handle_client_callback) { fprintf(stderr, "server_start: NULL handler\n"); return; }

    struct sigaction action = {0};
    action.sa_handler = stop_server_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) == -1 || sigaction(SIGTERM, &action, NULL) == -1) { perror("sigaction"); return; }

    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket == -1) { perror("socket"); return; }
    int reuse = 1;
    if (setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) { perror("setsockopt"); close(listening_socket); listening_socket = -1; return; }

    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server.sin_addr) != 1) { fprintf(stderr, "Invalid SERVER_IP: %s\n", SERVER_IP); close(listening_socket); listening_socket = -1; return; }
    if (bind(listening_socket, (struct sockaddr *)&server, sizeof(server)) == -1) { perror("bind"); close(listening_socket); listening_socket = -1; return; }
    if (listen(listening_socket, 32) == -1) { perror("listen"); close(listening_socket); listening_socket = -1; return; }

    printf("Banking server listening on %s:%d\n", SERVER_IP, SERVER_PORT);
    fflush(stdout);
    while (!stop_requested) {
        struct sockaddr_in peer = {0};
        socklen_t peer_size = sizeof(peer);
        int client_fd = accept(listening_socket, (struct sockaddr *)&peer, &peer_size);
        if (client_fd == -1) {
            if (errno == EINTR && !stop_requested) continue;
            if (!stop_requested) perror("accept");
            break;
        }

        ClientThreadContext *context = calloc(1, sizeof(*context));
        if (!context) { close(client_fd); continue; }
        context->client_fd = client_fd;
        context->handler = handle_client_callback;
        pthread_t thread;
        pthread_mutex_lock(&clients_mutex);
        context->next = clients;
        clients = context;
        active_clients++;
        int created = pthread_create(&thread, NULL, client_thread, context);
        if (created != 0) {
            remove_client_locked(context);
            pthread_mutex_unlock(&clients_mutex);
            close(client_fd);
            free(context);
            errno = created;
            perror("pthread_create");
            continue;
        }
        pthread_detach(thread);
        pthread_mutex_unlock(&clients_mutex);
    }

    /* Ask all client loops to stop, then wait before deleting shared session memory. */
    pthread_mutex_lock(&clients_mutex);
    for (ClientThreadContext *context = clients; context; context = context->next) shutdown(context->client_fd, SHUT_RDWR);
    while (active_clients > 0) pthread_cond_wait(&clients_condition, &clients_mutex);
    pthread_mutex_unlock(&clients_mutex);
    if (listening_socket != -1) { close(listening_socket); listening_socket = -1; }
}
