#include <stdio.h>
#include <unistd.h>

#include "connect.h"
#include "login.h"
#include "customer_services.h"
#include "bank_employee_services.h"
#include "bank_manager_services.h"
#include "administrator_services.h"
#include "client_common.h"
#include "../include/enums.h"

/* Main client menu. A fresh socket is opened after logout or session expiry. */
int main(void)
{
    int sock_fd = client_connect();

    for (;;) {
        printf("\n\n$---------$ Welcome to IIITB Bank $---------$\n\n");
        printf("1) Customer\n");
        printf("2) Bank Employee\n");
        printf("3) Bank Manager\n");
        printf("4) Administrator\n");
        printf("5) Exit\n");

        int choice = client_read_int("Enter the number corresponding to your role: ");
        if (choice == 5) {
            close(sock_fd);
            printf("Thank you for using IIITB Bank Services.\n");
            return 0;
        }

        enum User_Role role;
        switch (choice) {
            case 1: role = CUSTOMER; break;
            case 2: role = BANK_EMPLOYEE; break;
            case 3: role = BANK_MANAGER; break;
            case 4: role = ADMINISTRATOR; break;
            default:
                printf("Enter a valid role number.\n");
                continue;
        }

        int login_result = login(sock_fd, role);
        if (login_result == 1) {
            printf("Login failed. Please try again.\n");
            continue;
        }
        if (login_result == 2) {
            printf("This user is already logged in elsewhere.\n");
            continue;
        }
        if (login_result != 0) {
            /* A login I/O error means the old connection cannot be reused. */
            close(sock_fd);
            sock_fd = client_connect();
            continue;
        }

        switch (role) {
            case CUSTOMER: customer(sock_fd); break;
            case BANK_EMPLOYEE: bank_employee(sock_fd); break;
            case BANK_MANAGER: bank_manager(sock_fd); break;
            case ADMINISTRATOR: administrator(sock_fd); break;
        }

        /* Logout returns the server to its login loop; expiry may close the socket. */
        close(sock_fd);
        sock_fd = client_connect();
    }
}
