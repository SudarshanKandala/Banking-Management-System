#include "connect.h"
#include "user.h"
#include "login_check.h"
#include "database.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../include/comm.h"
#include "../socket_comm/socket_io.h"

/* Handle one TCP client. Login payloads are read here; authenticated service
   requests are then handed to user_main(), the only request receive loop. */
static void handle_client(int sock_fd)
{
    for (;;) {
        RequestHeader header = {0};
        ssize_t received = recv_all(sock_fd, &header, sizeof(header));
        if (received == 0) return;
        if (received != (ssize_t)sizeof(header)) { perror("receiving login/request header"); return; }
        if (header.Use_case != LOGIN) {
            LoginResponse response = {0};
            response.success = 1;
            snprintf(response.message, sizeof(response.message), "Please send a LOGIN request first.");
            send_all(sock_fd, &response, sizeof(response));
            return;
        }

        LoginRequest credentials = {0};
        received = recv_all(sock_fd, &credentials, sizeof(credentials));
        if (received != (ssize_t)sizeof(credentials)) { if (received < 0) perror("receiving login request"); return; }
        credentials.username[sizeof(credentials.username) - 1] = '\0';
        credentials.password[sizeof(credentials.password) - 1] = '\0';

        LoginResponse response = {0};
        response.success = login_check(header, credentials);
        response.session_id = response.success == 0 ? get_current_session_id() : -1;
        if (response.success == 0) snprintf(response.message, sizeof(response.message), "Login successful.");
        else if (response.success == 2) snprintf(response.message, sizeof(response.message), "This user is already logged in elsewhere.");
        else snprintf(response.message, sizeof(response.message), "Login failed. Check role, username, and password.");

        if (send_all(sock_fd, &response, sizeof(response)) != (ssize_t)sizeof(response)) { perror("sending login response"); logout_current_session(); return; }
        if (response.success == 0) {
            user_main(sock_fd);
            logout_current_session();
        }
    }
}

int main(void)
{
    /* Ensure all data files exist before accepting logins. */
    if (db_initialize_files() == -1) return EXIT_FAILURE;
    if (initialize_session_store() == -1) return EXIT_FAILURE;
    server_start(handle_client);
    destroy_session_store();
    return EXIT_SUCCESS;
}
