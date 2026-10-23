#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include "../include/types.h"
#include "../include/comm.h"
#include "../include/enums.h"
#include "customer_services.h"
#include "../socket_comm/socket_io.h"

void customer(int sock_fd){
    services:
    printf("What are you planning to do next:\n");
    printf("1) View Account Balance\n");
    printf("2) Deposit Money\n");
    printf("3) Withdraw Money\n");
    printf("4) Transfer Funds\n");
    printf("5) Apply for a Loan\n");
    printf("6) View Transaction History\n");
    printf("7) Change Password\n");
    printf("8) Add Feedback\n");
    printf("9) Logout\n");
    int choice;
    scanf("%d", &choice);
    enum Usecases usecase;
    RequestHeader header;
    header.role = CUSTOMER;
    header.message_size = 0;
    switch(choice){
        case 1:
            // View Account Balance
            usecase = VIEW_ACCOUNT_BALANCE;
            header.Use_case = usecase;
            if(send_all(sock_fd, &header, sizeof(header)) == -1){
                perror("sending header from client to server failed in View Account Balance\n");
                exit(EXIT_FAILURE);
            }
            float balance;
            ssize_t server_response_bytes = recv_all(sock_fd, &balance, sizeof(balance));
            if(server_response_bytes == sizeof(balance)){
                printf("Your account balance is: %0.2f\n", balance);
            } else {
                printf("Something went wrong in receiving account balance from server\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 2:
            // Deposit Money
            usecase = DEPOSIT_MONEY;
            header.Use_case = usecase;
            float amount;
            printf("Enter the amount to deposit: ");
            scanf("%f", &amount);
            DepositRequest deposit_request;
            deposit_request.amount = amount;
            header.message_size = sizeof(deposit_request);
            if(send_all(sock_fd, &header, sizeof(header)) == -1){
                perror("sending header from client to server failed in Deposit money\n");
                exit(EXIT_FAILURE);
            }
            if(send_all(sock_fd, &deposit_request, sizeof(deposit_request)) == -1){
                perror("sending header from client to server failed in Deposit money\n");
                exit(EXIT_FAILURE);
            }
            float remaining_balance;
            ssize_t server_response_bytes = recv_all(sock_fd, &remaining_balance, sizeof(remaining_balance));
            if(server_response_bytes == sizeof(remaining_balance)){
                printf("Deposit successful. Your remaining balance is: %.2f\n", remaining_balance);
            } else {
                printf("Deposit failed\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 3:
            // Withdraw Money
            usecase = WITHDRAW_MONEY;
            header.Use_case = usecase;
            float amount;
            printf("Enter the amount to withdraw: ");
            scanf("%f", &amount);
            WithdrawRequest withdraw_request;
            withdraw_request.amount = amount;
            header.message_size = sizeof(withdraw_request);
            if(send_all(sock_fd, &header, sizeof(header)) == -1){
                perror("sending header from client to server failed in Withdraw money\n");
                exit(EXIT_FAILURE);
            }
            if(send_all(sock_fd, &withdraw_request, sizeof(withdraw_request)) == -1){
                perror("sending header from client to server failed in Withdraw money\n");
                exit(EXIT_FAILURE);
            }
            float remaining_balance;
            ssize_t server_response_bytes = recv_all(sock_fd, &remaining_balance, sizeof(remaining_balance));
            if(server_response_bytes == sizeof(remaining_balance) && remaining_balance >= 0.0f)
                printf("Withdrawal successful. Your remaining balance is: %.2f\n", remaining_balance);
            else if(remaining_balance == -1.0f)
                printf("Insufficient funds for withdrawal\n");
            else {
                printf("Something went wrong in receiving withdrawal response from server\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 4:
            // Transfer Funds
            usecase = TRANSFER_FUNDS;
            header.Use_case = usecase;
            float amount;
            long int recipient_account;
            printf("Enter the recipient's account number: ");
            scanf("%ld", &recipient_account);
            printf("Enter the amount to transfer: ");
            scanf("%f", &amount);
            TransferRequest transfer_request = {
                .recipient_account = recipient_account,
                .amount = amount
            };
            header.message_size = sizeof(transfer_request);
            if(send_all(sock_fd, &header, sizeof(header)) == -1){
                perror("sending header from client to server failed in Transfer funds\n");
                exit(EXIT_FAILURE);
            }
            if(send_all(sock_fd, &transfer_request, sizeof(transfer_request)) == -1){
                perror("sending transfer request from client to server failed\n");
                exit(EXIT_FAILURE);
            }
            float remaining_balance;
            ssize_t server_response_bytes = recv_all(sock_fd, &remaining_balance, sizeof(remaining_balance));
            if(server_response_bytes == sizeof(remaining_balance))
                printf("Transfer successful. Your remaining balance is: %.2f\n", remaining_balance);
            else if(remaining_balance == -1.0f)
                printf("Insufficient funds for transfer\n");
            else if(remaining_balance == -2.0f)
                printf("Invalid recipient account number\n"); 
            else {
                printf("Something went wrong in receiving transfer response from server\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 5:
            // Apply for a Loan
            usecase = APPLY_LOAN;
            header.Use_case = usecase;
            LoanRequest loan_request;
            printf("Enter the loan type (0: HOME_LOAN, 1: CAR_LOAN, 2: PERSONAL_LOAN, 3: EDUCATION_LOAN, 4: BUSINESS_LOAN): ");
            int loan_type_input;
            scanf("%d", &loan_type_input);
            if(loan_type_input < 0 || loan_type_input > 4){
                printf("Invalid loan type selected\n");
                exit(EXIT_FAILURE);
            }
            loan_request.loan_type = (enum Loan_Type)loan_type_input;
            printf("Enter the loan amount: ");
            scanf("%f", &loan_request.amount);
            printf("Enter the tenure in months: ");
            scanf("%d", &loan_request.tenure_months);
            header.message_size = sizeof(loan_request);
            if(send_all(sock_fd, &header, sizeof(header)) == -1){
                perror("sending header from client to server failed in Apply for a Loan\n");
                exit(EXIT_FAILURE);
            }
            if(send_all(sock_fd, &loan_request, sizeof(loan_request)) == -1){
                perror("sending loan request from client to server failed\n");
                exit(EXIT_FAILURE);
            }
            bool loan_approval;
            ssize_t server_response_bytes = read(sock_fd, &loan_approval, sizeof(loan_approval));
            if(server_response_bytes == sizeof(loan_approval)){
                if(loan_approval){
                    printf("Loan application successful. Your loan is approved.\n");
                } else {
                    printf("Loan application unsuccessful. Your loan is not approved.\n");
                }
            } else {
                printf("Something went wrong in receiving loan application response from server\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 6:
            // View Transaction History
            usecase = TRANSACTION_HISTORY;
            header.Use_case = usecase;
            if (send_all(sock_fd, &header, sizeof(header)) != (ssize_t)sizeof(header)) {
                perror("Sending transaction history request failed");
                exit(EXIT_FAILURE);
            }
            int count;
            ssize_t server_response_bytes;
            // Receive the number of transaction entries.
            server_response_bytes = recv_all(sock_fd, &count, sizeof(count));
            if (server_response_bytes != (ssize_t)sizeof(count)) {
                fprintf(stderr, "Failed to receive transaction count\n");
                exit(EXIT_FAILURE);
            }
            if (count < 0 || count > 100) {
                fprintf(stderr, "Invalid transaction count received\n");
                exit(EXIT_FAILURE);
            }
            PassbookEntry passbook_entry[100];
            // Receive each entry separately.
            for (int i = 0; i < count; i++) {
                server_response_bytes = recv_all(sock_fd, &passbook_entry[i], sizeof(passbook_entry[i]));
                if (server_response_bytes != (ssize_t)sizeof(passbook_entry[i])) {
                    fprintf(stderr, "Failed to receive transaction entry\n");
                    exit(EXIT_FAILURE);
                }
            }
            printf("\nTransaction History:\n\n");
            for (int i = 0; i < count; i++) {
                printf("Entry ID: %d\n", passbook_entry[i].entryID);
                printf("Account ID: %d\n", passbook_entry[i].accountID);
                printf("Transaction ID: %d\n",passbook_entry[i].transactionID);
                printf("Date and Time: %s", ctime(&passbook_entry[i].date_time));
                printf("Particulars: %s\n", passbook_entry[i].particulars);
                printf("Debit: %.2f\n", passbook_entry[i].debit);
                printf("Credit: %.2f\n", passbook_entry[i].credit);
                printf("Balance After: %.2f\n\n", passbook_entry[i].balance_after);
            }
            break;

        case 7:
            // Change Password
            usecase = CHANGE_PASSWORD;
            header.Use_case = usecase;
            char new_password[128];
            printf("Enter the new password: ");
            scanf("%s", new_password);
            header.message_size = sizeof(new_password);
            if(send_all(sock_fd, &header, sizeof(header)) == -1){
                perror("sending header from client to server failed in Change Password\n");
                exit(EXIT_FAILURE);
            }
            if(send_all(sock_fd, &new_password, sizeof(new_password)) == -1){
                perror("sending new password from client to server failed\n");
                exit(EXIT_FAILURE);
            }
            bool password_change_success;
            ssize_t server_response_bytes = read(sock_fd, &password_change_success, sizeof(password_change_success));
            if(server_response_bytes == sizeof(password_change_success)){
                if(password_change_success){
                    printf("Password changed successfully\n");
                } else {
                    printf("Password change failed\n");
                }
            } else {
                printf("Something went wrong in receiving password change response from server\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 8:
            // Add Feedback
            usecase = ADD_FEEDBACK;
            header.Use_case = usecase;
            FeedbackRequest feedback_request;
            printf("Enter your feedback: ");
            scanf(" %[^\n]s", feedback_request.feedback);
            header.message_size = sizeof(feedback_request);
            if(send_all(sock_fd, &header, sizeof(header)) == -1){
                perror("sending header from client to server failed in Add Feedback\n");
                exit(EXIT_FAILURE);
            }
            if(send_all(sock_fd, &feedback_request, sizeof(feedback_request)) == -1){
                perror("sending feedback from client to server failed\n");
                exit(EXIT_FAILURE);
            }
            bool feedback_success;
            ssize_t server_response_bytes = read(sock_fd, &feedback_success, sizeof(feedback_success));
            if(server_response_bytes == sizeof(feedback_success)){
                if(feedback_success){
                    printf("Feedback submitted successfully\n");
                } else {
                    printf("Feedback submission failed\n");
                }
            } else {
                printf("Something went wrong in receiving feedback submission response from server\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 9:
            // Logout
            usecase = LOGOUT;
            header.Use_case = usecase;
            if(send_all(sock_fd, &header, sizeof(header)) == -1){
                perror("sending header from client to server failed in Logout\n");
                exit(EXIT_FAILURE);
            }
            bool logout_success;
            ssize_t server_response_bytes = read(sock_fd, &logout_success, sizeof(logout_success));
            if(server_response_bytes == sizeof(logout_success)){
                if(logout_success){
                    printf("Logged out successfully\n");
                    return; // Exit the customer function after logout
                } else {
                    printf("Logout failed\n");
                }
            } else {
                printf("Something went wrong in receiving logout response from server\n");
                exit(EXIT_FAILURE);
            }
            break;
    }
    goto services;
}