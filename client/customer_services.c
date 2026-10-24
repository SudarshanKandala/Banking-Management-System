#include <stdio.h>
#include <string.h>

#include "customer_services.h"
#include "client_common.h"
#include "../include/comm.h"
#include "../include/enums.h"

/* Present customer use cases and continue until logout, expiry, or an I/O error. */
void customer(int sock_fd)
{
    for (;;) {
        printf("\n========== CUSTOMER SERVICES ==========\n");
        printf("1) View account balance\n");
        printf("2) Deposit money\n");
        printf("3) Withdraw money\n");
        printf("4) Transfer funds\n");
        printf("5) Apply for a loan\n");
        printf("6) View transaction history / passbook\n");
        printf("7) Change password\n");
        printf("8) Add feedback\n");
        printf("9) Logout\n");

        int choice = client_read_int("Select a service: ");
        enum Usecases usecase;
        ServiceRequest request = {0};
        ServiceResponse response = {0};
        int show_balance = 0;

        switch (choice) {
            case 1:
                usecase = VIEW_ACCOUNT_BALANCE;
                show_balance = 1;
                break;
            case 2:
                usecase = DEPOSIT_MONEY;
                request.money.account_number = client_read_long("Account number: ");
                request.money.amount = client_read_float("Amount to deposit: ");
                show_balance = 1;
                break;
            case 3:
                usecase = WITHDRAW_MONEY;
                request.money.account_number = client_read_long("Account number: ");
                request.money.amount = client_read_float("Amount to withdraw: ");
                show_balance = 1;
                break;
            case 4:
                usecase = TRANSFER_FUNDS;
                request.transfer.source_account_number = client_read_long("Your source account number: ");
                request.transfer.recipient_account_number = client_read_long("Recipient account number: ");
                request.transfer.amount = client_read_float("Amount to transfer: ");
                show_balance = 1;
                break;
            case 5: {
                usecase = APPLY_LOAN;
                int loan_type = client_read_int("Loan type (0 Home, 1 Car, 2 Personal, 3 Education, 4 Business): ");
                if (loan_type < HOME_LOAN || loan_type > BUSINESS_LOAN) {
                    printf("Invalid loan type.\n");
                    continue;
                }
                request.apply_loan.loan_type = (enum Loan_Type)loan_type;
                request.apply_loan.amount = client_read_float("Requested loan amount: ");
                request.apply_loan.tenure_months = client_read_int("Tenure in months: ");
                break;
            }
            case 6:
                usecase = TRANSACTION_HISTORY;
                break;
            case 7:
                usecase = CHANGE_PASSWORD;
                client_read_line("New password: ", request.change_password.new_password,
                                 sizeof(request.change_password.new_password));
                break;
            case 8:
                usecase = ADD_FEEDBACK;
                client_read_line("Enter your feedback: ", request.feedback.message,
                                 sizeof(request.feedback.message));
                break;
            case 9:
                usecase = LOGOUT;
                break;
            default:
                printf("Invalid choice. Select a number from 1 to 9.\n");
                continue;
        }

        /* Every request sends the full ServiceRequest union expected by the server. */
        int result = client_service_request(sock_fd, CUSTOMER, usecase, &request, &response);
        if (result != 0) return;
        if (response.status == 0 && show_balance) printf("Current balance: %.2f\n", response.balance);
        if (response.status == 0 && response.record_id > 0 && usecase == APPLY_LOAN)
            printf("Loan application ID: %d\n", response.record_id);
        if (usecase == LOGOUT) return;
    }
}
