#include <stdio.h>
#include <string.h>

#include "login.h"
#include "client_common.h"
#include "../include/comm.h"
#include "../socket_comm/socket_io.h"

/* Send credentials using the same LoginRequest structure the server expects. */
int login(int sock_fd, enum User_Role role)
{
    LoginRequest credentials = {0};
    client_read_line("Enter username: ", credentials.username, sizeof(credentials.username));
    client_read_line("Enter password: ", credentials.password, sizeof(credentials.password));

    RequestHeader header = {0};
    header.role = role;
    header.Use_case = LOGIN;

    if (send_all(sock_fd, &header, sizeof(header)) != (ssize_t)sizeof(header)) {
        perror("Sending login header");
        return -1;
    }
    if (send_all(sock_fd, &credentials, sizeof(credentials)) != (ssize_t)sizeof(credentials)) {
        perror("Sending login credentials");
        return -1;
    }

    LoginResponse response = {0};
    if (recv_all(sock_fd, &response, sizeof(response)) != (ssize_t)sizeof(response)) {
        perror("Receiving login response");
        return -1;
    }

    response.message[sizeof(response.message) - 1] = '\0';
    if (response.message[0] != '\0') printf("%s\n", response.message);

    /* The server convention is 0=success, 1=failed, and 2=already logged in. */
    return response.success;
}
