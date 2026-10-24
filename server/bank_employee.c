#include "services.h"
#include "service_common.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "login_check.h"

/* Disable a partially added user if its required Customer profile could not be written. */
static int deactivate_user_record(void *record, void *context)
{
    User *user = record;
    (void)context;
    user->is_active = false;
    user->isActive = false;
    return 0;
}

/* User IDs and role-specific profile IDs deliberately match for simple lookups. */
static int username_exists(const char *username)
{
    off_t count;
    if (db_count_records(DB_USERS, sizeof(User), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        User row;
        if (db_read_record(DB_USERS, sizeof(row), i, &row) != 0) return -1;
        if (row.is_active && strcmp(row.username, username) == 0) return 1;
    }
    return 0;
}

/* Add the login identity and customer profile; opening an account remains a separate use case. */
static void employee_add_customer(const AddCustomerRequest *input, ServiceOutput *output)
{
    if (input->username[0] == '\0' || input->password[0] == '\0' || input->name[0] == '\0') {
        service_error(output, "Username, password, and customer name are required."); return;
    }
    int exists = username_exists(input->username);
    if (exists != 0) { service_error(output, exists < 0 ? "Could not check usernames." : "That username already exists."); return; }

    User user = {0};
    if (service_next_id(DB_USERS, sizeof(user), offsetof(User, userID), &user.userID) == -1) { service_error(output, "Could not allocate user ID."); return; }
    snprintf(user.username, sizeof(user.username), "%s", input->username);
    if (service_hash_password(input->password, user.passwordHash) == -1) { service_error(output, "Could not hash the initial password."); return; }
    user.Role = CUSTOMER;
    user.isActive = true;
    user.is_active = true;
    user.last_login = 0;

    Customer customer = {0};
    customer.CustomerID = user.userID;
    snprintf(customer.name, sizeof(customer.name), "%s", input->name);
    snprintf(customer.email, sizeof(customer.email), "%s", input->email);
    customer.phone = input->phone;
    snprintf(customer.address, sizeof(customer.address), "%s", input->address);
    customer.is_active = true;

    off_t user_index;
    if (db_append_record(DB_USERS, sizeof(user), &user, &user_index) == -1) { service_error(output, "Could not save the customer login record."); return; }
    if (db_append_record(DB_CUSTOMERS, sizeof(customer), &customer, NULL) == -1) {
        db_modify_record(DB_USERS, sizeof(User), user_index, deactivate_user_record, NULL);
        service_error(output, "Customer profile could not be saved; the login record was tombstoned."); return;
    }
    char details[128];
    snprintf(details, sizeof(details), "Customer userID=%d created", user.userID);
    service_add_log("ADD_NEW_CUSTOMER", details);
    output->response.record_id = customer.CustomerID;
    service_success(output, "Customer created. Use OPEN_CUSTOMER_ACCOUNT to open a bank account.");
}

typedef struct { ModifyPersonRequest input; } CustomerModifyContext;
static int modify_customer(void *record, void *context)
{
    Customer *customer = record;
    CustomerModifyContext *change = context;
    if (!customer->is_active || customer->CustomerID != change->input.target_id) return 1;
    snprintf(customer->name, sizeof(customer->name), "%s", change->input.name);
    snprintf(customer->email, sizeof(customer->email), "%s", change->input.email);
    customer->phone = change->input.phone;
    snprintf(customer->address, sizeof(customer->address), "%s", change->input.address);
    return 0;
}

/* Staff can change customer details, but not a bank employee's details. */
static void employee_modify_customer(const ModifyPersonRequest *input, ServiceOutput *output)
{
    if (input->target_role != CUSTOMER || input->target_id <= 0) { service_error(output, "Bank employees may modify customer details only."); return; }
    Customer customer;
    off_t index;
    if (db_find_customer(input->target_id, &customer, &index) != 0) { service_error(output, "Customer was not found."); return; }
    CustomerModifyContext context = {0};
    context.input = *input;
    int status = db_modify_record(DB_CUSTOMERS, sizeof(Customer), index, modify_customer, &context);
    if (status != 0) { service_error(output, "Customer details could not be modified."); return; }
    service_add_log("MODIFY_CUSTOMER_DETAILS", "Bank employee modified customer profile");
    service_success(output, "Customer details modified successfully.");
}

/* Open a new account for an existing customer and optionally record an opening deposit. */
static void employee_open_account(const OpenAccountRequest *input, ServiceOutput *output)
{
    Customer customer;
    if (input->customerID <= 0 || db_find_customer(input->customerID, &customer, NULL) != 0) { service_error(output, "Customer does not exist."); return; }
    if (input->account_type < SAVINGS || input->account_type > RECURRING_DEPOSIT || input->initial_deposit < 0.0f || !isfinite(input->initial_deposit)) { service_error(output, "Invalid account type or opening deposit."); return; }

    BankAccount account = {0};
    if (service_next_id(DB_ACCOUNTS, sizeof(account), offsetof(BankAccount, accountID), &account.accountID) == -1) { service_error(output, "Could not allocate account ID."); return; }
    account.account_number = 1000000000L + account.accountID;
    account.account_type = input->account_type;
    account.balance = input->initial_deposit;
    account.account_status = ACTIVE;
    account.branchID = input->branchID;
    account.customerID = customer.CustomerID;
    account.opened_date = time(NULL);
    account.is_active = true;
    if (db_append_record(DB_ACCOUNTS, sizeof(account), &account, NULL) == -1) { service_error(output, "Could not save the new account."); return; }

    if (input->initial_deposit > 0.0f) {
        int transaction_id = 0, passbook_id = 0;
        int history_status = service_append_transaction(account.accountID, DEPOSIT, input->initial_deposit, "Opening deposit", 0, TRANSACTION_SUCCESS, &transaction_id);
        if (history_status == 0) history_status = service_append_passbook(account.accountID, transaction_id, "Opening deposit", 0.0f, input->initial_deposit, account.balance, &passbook_id);
        if (history_status != 0) {
            if (passbook_id > 0) db_tombstone_record(DB_PASSBOOK, sizeof(PassbookEntry), offsetof(PassbookEntry, entryID), offsetof(PassbookEntry, is_active), passbook_id);
            if (transaction_id > 0) db_tombstone_record(DB_TRANSACTIONS, sizeof(Transaction), offsetof(Transaction, transactionID), offsetof(Transaction, is_active), transaction_id);
            db_tombstone_record(DB_ACCOUNTS, sizeof(BankAccount), offsetof(BankAccount, accountID), offsetof(BankAccount, is_active), account.accountID);
            service_error(output, "Opening deposit history failed; the new account was tombstoned."); return;
        }
    }
    char details[160];
    snprintf(details, sizeof(details), "Account %ld opened for customer %d", account.account_number, customer.CustomerID);
    service_add_log("OPEN_CUSTOMER_ACCOUNT", details);
    output->response.record_id = account.accountID;
    output->response.balance = account.balance;
    service_success(output, "Account opened successfully.");
}

typedef struct { enum Account_Status status; } CloseAccountContext;
static int close_account(void *record, void *context)
{
    BankAccount *account = record;
    (void)context;
    if (!account->is_active || account->account_status == CLOSED) return 1;
    account->account_status = CLOSED; /* Keep the record active so its history remains available. */
    return 0;
}

static void employee_close_account(long int account_number, ServiceOutput *output)
{
    BankAccount account;
    off_t index;
    if (service_find_customer_account_number(account_number, &account, &index) != 0) { service_error(output, "Account was not found."); return; }
    int status = db_modify_record(DB_ACCOUNTS, sizeof(BankAccount), index, close_account, NULL);
    if (status != 0) { service_error(output, "Account is already closed or could not be updated."); return; }
    char details[128];
    snprintf(details, sizeof(details), "Account %ld closed", account_number);
    service_add_log("CLOSE_CUSTOMER_ACCOUNT", details);
    service_success(output, "Account closed. Historical transactions were retained.");
}

/* Process a decision on a loan application assigned to the current employee. */
typedef struct { int employee_id; int approve; } LoanDecisionContext;
static int update_loan_decision(void *record, void *context)
{
    LoanApplication *application = record;
    LoanDecisionContext *decision = context;
    if (!application->is_active || application->status != LOAN_PENDING || application->processed_by != decision->employee_id) return 1;
    application->status = decision->approve ? LOAN_APPROVED : LOAN_REJECTED;
    return 0;
}

static void employee_decide_loan(const DecideLoanRequest *input, ServiceOutput *output)
{
    int employee_id = get_current_user_id();
    LoanApplication application;
    off_t index;
    if (input->applicationID <= 0 || db_find_loan_application(input->applicationID, &application, &index) != 0 || application.processed_by != employee_id || application.status != LOAN_PENDING) {
        service_error(output, "Application is not pending or is not assigned to you."); return;
    }

    /* For approval, create the loan first so an application is never marked approved
       without a corresponding Loan record. A failed application update tombstones it. */
    int loan_id = 0;
    if (input->approve) {
        LoanType type;
        if (db_find_loan_type(application.loan_type, &type, NULL) != 0) { service_error(output, "Loan type configuration is missing."); return; }

        Loan loan = {0};
        if (service_next_id(DB_LOANS, sizeof(loan), offsetof(Loan, loanID), &loan.loanID) == -1) { service_error(output, "Could not allocate loan ID."); return; }
        loan.applicationID = application.applicationID;
        loan.customerID = application.customerID;
        loan.loanTypeID = (int)application.loan_type;
        loan.amount = application.amount_requested;
        loan.interest_rate = type.interest_rate;
        loan.tenure_months = application.tenure_months;
        loan.start_date = time(NULL);
        loan.status = LOAN_APPROVED;
        loan.is_active = true;
        if (db_append_record(DB_LOANS, sizeof(loan), &loan, NULL) == -1) { service_error(output, "Could not save the loan record."); return; }
        loan_id = loan.loanID;
    }

    LoanDecisionContext context = {employee_id, input->approve != 0};
    int update = db_modify_record(DB_LOAN_APPLICATIONS, sizeof(LoanApplication), index, update_loan_decision, &context);
    if (update != 0) {
        if (loan_id > 0) db_tombstone_record(DB_LOANS, sizeof(Loan), offsetof(Loan, loanID), offsetof(Loan, is_active), loan_id);
        service_error(output, "Loan application could not be updated; any new loan record was tombstoned."); return;
    }

    if (input->approve) service_add_notification(application.customerID, "Your loan application was approved.");
    else service_add_notification(application.customerID, "Your loan application was rejected.");
    service_add_log("ACCEPT_REJECT_LOANS", input->approve ? "Loan application approved" : "Loan application rejected");
    output->response.record_id = loan_id > 0 ? loan_id : application.applicationID;
    service_success(output, input->approve ? "Loan application approved." : "Loan application rejected.");
}

/* List assigned applications including completed ones for the employee's history. */
static int employee_list_assigned_loans(int employee_id, ServiceOutput *output)
{
    off_t count;
    if (db_count_records(DB_LOAN_APPLICATIONS, sizeof(LoanApplication), &count) == -1) return -1;
    output->response.list_kind = RESPONSE_LIST_LOAN_APPLICATIONS;
    output->response.record_count = 0;
    for (off_t i = 0; i < count && output->response.record_count < MAX_LIST_RECORDS; i++) {
        LoanApplication row;
        if (db_read_record(DB_LOAN_APPLICATIONS, sizeof(row), i, &row) != 0) return -1;
        if (row.is_active && row.processed_by == employee_id) output->loan_applications[output->response.record_count++] = row;
    }
    return 0;
}

/* Bank employee entry point: one authenticated use case per call. */
void bank_employee_main(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output)
{
    switch (usecase) {
        case CHANGE_PASSWORD:
            service_change_password(request->change_password.new_password, output);
            break;
        case ADD_NEW_CUSTOMER:
            employee_add_customer(&request->add_customer, output);
            break;
        case MODIFY_CUSTOMER_DETAILS:
            employee_modify_customer(&request->modify_person, output);
            break;
        case OPEN_CUSTOMER_ACCOUNT:
            employee_open_account(&request->open_account, output);
            break;
        case CLOSE_CUSTOMER_ACCOUNT:
            employee_close_account(request->money.account_number, output);
            break;
        case PROCESS_LOAN_APPLICATIONS: {
            int employee_id = get_current_user_id();
            if (service_collect_pending_loans(employee_id, 1, output) == -1) service_error(output, "Could not read assigned loan applications.");
            else service_success(output, "Pending loan applications assigned to you follow.");
            break;
        }
        case ACCEPT_REJECT_LOANS:
            employee_decide_loan(&request->decide_loan, output);
            break;
        case VIEW_LOAN_APPLICATIONS:
            if (employee_list_assigned_loans(get_current_user_id(), output) == -1) service_error(output, "Could not read assigned loan applications.");
            else service_success(output, "Your assigned loan applications follow.");
            break;
        case VIEW_CUSTOMER_TRANSACTIONS:
            if (db_find_customer(request->customer_lookup.customerID, &(Customer){0}, NULL) != 0 ||
                service_collect_passbook(request->customer_lookup.customerID, output) == -1) service_error(output, "Customer or transaction history was not found.");
            else service_success(output, "Customer passbook entries follow.");
            break;
        case LOGOUT:
            output->end_session = 1;
            service_success(output, "Logged out successfully.");
            break;
        default:
            service_error(output, "You are not allowed to use that service.");
            break;
    }
}
