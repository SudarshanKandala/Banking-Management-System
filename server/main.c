#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "connect.h"
#include "../include/comm.h"
#include "../include/enums.h"
#include "../socket_comm/socket_io.h"
#include "login_check.h"
#include "user.h"

void handle_client(int sock_fd)
{
    while (1) {
        RequestHeader header;
        ssize_t received_bytes = recv_all(sock_fd, &header, sizeof(header));

        if (received_bytes == 0) {
            printf("Connection closed by client\n");
            return;
        }

        if (received_bytes != (ssize_t)sizeof(header)) {
            if (received_bytes < 0) perror("Receiving request header failed");
            else fprintf(stderr, "Incomplete request header\n");
            return;
        }

        if (header.Use_case != LOGIN) {
            fprintf(stderr, "Expected LOGIN request before authentication\n");
            return;
        }

        LoginRequest login_details;
        received_bytes = recv_all(sock_fd, &login_details, sizeof(login_details));
        if (received_bytes != (ssize_t)sizeof(login_details)) {
            if (received_bytes < 0) perror("Receiving login payload failed");
            else fprintf(stderr, "Incomplete login payload\n");
            return;
        }

        LoginResponse response = {0};
        response.success = login_check(header, login_details);
        if (send_all(sock_fd, &response, sizeof(response)) != (ssize_t)sizeof(response)) {
            perror("Sending login response failed");
            logout_current_session();
            return;
        }

        if (response.success == 0) {
            user_main(sock_fd);
            logout_current_session();
            /* If user_main returns after logout, this connection may try another login. */
        }
        /* Return codes are intentional: 0 = success, 1 = failed, 2 = logged in elsewhere. */
    }
}

int main(void)
{
    if (initialize_session_store() == -1) return EXIT_FAILURE;
    server_start(handle_client);
    destroy_session_store();
    return 0;
}
