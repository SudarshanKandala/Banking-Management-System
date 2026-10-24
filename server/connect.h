#ifndef SERVER_CONNECT_H
#define SERVER_CONNECT_H

typedef void (*client_handler_fn)(int client_fd);
void server_start(client_handler_fn handle_client);

#endif
