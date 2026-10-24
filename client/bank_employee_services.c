#include <stdio.h>
#include <string.h>

#include "bank_employee_services.h"
#include "client_common.h"
#include "../include/comm.h"
#include "../include/enums.h"

/* Collect customer contact details shared by the add and modify services. */
static void read_customer_details(ModifyPersonRequest *person)
{
    client_read_line("Customer name: ", person->name, sizeof(person->name));
    client_read_line("Email: ", person->email, sizeof(person->email));
    person->phone = client_read_long("Phone number (digits only): ");
    client_read_line("Address: ", person->address, sizeof(person->address));
}

/* Present the services allowed to a bank employee. The server enforces permissions too. */
void bank_employee(int sock_fd)
{
    for (;;) {
        printf("\n========== BANK EMPLOYEE SERVICES ==========\n");
        printf("1) Add a new customer\n");
        printf("2) Modify customer details\n");
        printf("3) Open a customer account\n");
        printf("4) Close a customer account\n");
        printf("5) Process pending loan applications assigned to me\n");
        printf("6) Accept or reject an assigned loan application\n");
        printf("7) View my assigned loan applications\n");
        printf("8) View a customer's transactions / passbook\n");
        printf("9) Change password\n");
        printf("10) Logout\n");

        int choice = client_read_int("Select a service: ");
        enum Usecases usecase;
        ServiceRequest request = {0};
        ServiceResponse response = {0};

        switch (choice) {
            case 1:
                usecase = ADD_NEW_CUSTOMER;
                client_read_line("New username: ", request.add_customer.username, sizeof(request.add_customer.username));
                client_read_line("Initial password: ", request.add_customer.password, sizeof(request.add_customer.password));
                client_read_line("Customer name: ", request.add_customer.name, sizeof(request.add_customer.name));
                client_read_line("Email: ", request.add_customer.email, sizeof(request.add_customer.email));
                request.add_customer.phone = client_read_long("Phone number (digits only): ");
                client_read_line("Address: ", request.add_customer.address, sizeof(request.add_customer.address));
                break;
            case 2:
                usecase = MODIFY_CUSTOMER_DETAILS;
                request.modify_person.target_role = CUSTOMER;
                request.modify_person.target_id = client_read_int("Customer ID: ");
                read_customer_details(&request.modify_person);
                break;
            case 3: {
                usecase = OPEN_CUSTOMER_ACCOUNT;
                request.open_account.customerID = client_read_int("Customer ID: ");
                int type = client_read_int("Account type (0 Savings, 1 Current, 2 Fixed deposit, 3 Recurring deposit): ");
                if (type < SAVINGS || type > RECURRING_DEPOSIT) {
                    printf("Invalid account type.\n");
                    continue;
                }
                request.open_account.account_type = (enum Account_Type)type;
                request.open_account.branchID = client_read_int("Branch ID: ");
                request.open_account.initial_deposit = client_read_float("Initial deposit (0 if none): ");
                break;
            }
            case 4:
                usecase = CLOSE_CUSTOMER_ACCOUNT;
                request.money.account_number = client_read_long("Account number to close: ");
                break;
            case 5:
                usecase = PROCESS_LOAN_APPLICATIONS;
                break;
            case 6:
                usecase = ACCEPT_REJECT_LOANS;
                request.decide_loan.applicationID = client_read_int("Loan application ID: ");
                request.decide_loan.approve = client_read_int("Decision (1 approve, 0 reject): ") != 0;
                break;
            case 7:
                usecase = VIEW_LOAN_APPLICATIONS;
                break;
            case 8:
                usecase = VIEW_CUSTOMER_TRANSACTIONS;
                request.customer_lookup.customerID = client_read_int("Customer ID: ");
                break;
            case 9:
                usecase = CHANGE_PASSWORD;
                client_read_line("New password: ", request.change_password.new_password,
                                 sizeof(request.change_password.new_password));
                break;
            case 10:
                usecase = LOGOUT;
                break;
            default:
                printf("Invalid choice. Select a number from 1 to 10.\n");
                continue;
        }

        int result = client_service_request(sock_fd, BANK_EMPLOYEE, usecase, &request, &response);
        if (result != 0) return;
        if (response.status == 0 && response.record_id > 0 &&
            (usecase == ADD_NEW_CUSTOMER || usecase == OPEN_CUSTOMER_ACCOUNT))
            printf("Created record ID: %d\n", response.record_id);
        if (response.status == 0 && usecase == OPEN_CUSTOMER_ACCOUNT)
            printf("Opening balance: %.2f\n", response.balance);
        if (usecase == LOGOUT) return;
    }
}
