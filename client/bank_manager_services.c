#include <stdio.h>

#include "bank_manager_services.h"
#include "client_common.h"
#include "../include/comm.h"
#include "../include/enums.h"

/* Present account-control, loan-assignment, feedback, and password services. */
void bank_manager(int sock_fd)
{
    for (;;) {
        printf("\n========== BANK MANAGER SERVICES ==========\n");
        printf("1) Activate, deactivate, or freeze an account\n");
        printf("2) Assign a pending loan application to an employee\n");
        printf("3) List open feedback or update feedback status\n");
        printf("4) Change password\n");
        printf("5) Logout\n");

        int choice = client_read_int("Select a service: ");
        enum Usecases usecase;
        ServiceRequest request = {0};
        ServiceResponse response = {0};

        switch (choice) {
            case 1: {
                usecase = ACTIVATE_DEACTIVATE_CUSTOMER_ACCOUNT;
                request.account_status.account_number = client_read_long("Account number: ");
                int status = client_read_int("Status (0 ACTIVE, 1 INACTIVE, 3 FROZEN): ");
                if (status != ACTIVE && status != INACTIVE && status != FROZEN) {
                    printf("Choose only 0 (ACTIVE), 1 (INACTIVE), or 3 (FROZEN).\n");
                    continue;
                }
                request.account_status.account_status = (enum Account_Status)status;
                break;
            }
            case 2:
                usecase = ASSIGN_LOAN_APPLICATION;
                request.assign_loan.applicationID = client_read_int("Loan application ID: ");
                request.assign_loan.employeeID = client_read_int("Bank employee ID: ");
                break;
            case 3:
                usecase = REVIEW_CUSTOMER_FEEDBACK;
                request.review_feedback.feedbackID = client_read_int("Feedback ID (0 to list open feedback): ");
                if (request.review_feedback.feedbackID != 0) {
                    int status = client_read_int("New feedback status (0 OPEN, 1 RESOLVED, 2 CLOSED): ");
                    if (status < FEEDBACK_OPEN || status > FEEDBACK_CLOSED) {
                        printf("Invalid feedback status.\n");
                        continue;
                    }
                    request.review_feedback.status = (enum FeedBack_Status)status;
                }
                break;
            case 4:
                usecase = CHANGE_PASSWORD;
                client_read_line("New password: ", request.change_password.new_password,
                                 sizeof(request.change_password.new_password));
                break;
            case 5:
                usecase = LOGOUT;
                break;
            default:
                printf("Invalid choice. Select a number from 1 to 5.\n");
                continue;
        }

        int result = client_service_request(sock_fd, BANK_MANAGER, usecase, &request, &response);
        if (result != 0) return;
        if (usecase == LOGOUT) return;
    }
}
