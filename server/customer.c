#include "services.h"
#include "service_common.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "login_check.h"

/* Resolve the customer profile for the currently logged-in customer. The project
   uses the same numeric ID for User.userID and Customer.CustomerID. */
static int current_customer(Customer *customer, off_t *index)
{
    int user_id = get_current_user_id();
    if (user_id < 0) return -1;
    return db_find_customer(user_id, customer, index);
}

/* Update a customer's account balance and append transaction + passbook records. */
static void customer_money(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output)
{
    Customer customer;
    if (current_customer(&customer, NULL) != 0) { service_error(output, "Customer profile was not found."); return; }
    long int account_number = request->money.account_number;
    float amount = request->money.amount;
    if (account_number <= 0 || !(amount > 0.0f) || !isfinite(amount)) { service_error(output, "Enter a valid account number and positive amount."); return; }

    BankAccount account;
    off_t account_index;
    if (service_find_customer_account_number(account_number, &account, &account_index) != 0 || !service_account_belongs_to_customer(&account, customer.CustomerID)) {
        service_error(output, "Account was not found for this customer."); return;
    }
    float balance = 0.0f;
    float delta = usecase == DEPOSIT_MONEY ? amount : -amount;
    int changed = service_make_account_adjustment(account_index, delta, &balance);
    if (changed == 1) { service_error(output, usecase == WITHDRAW_MONEY ? "Account is inactive or has insufficient funds." : "Account is not active."); return; }
    if (changed != 0) { service_error(output, "Could not update the account record."); return; }

    char description[128];
    snprintf(description, sizeof(description), "%s", usecase == DEPOSIT_MONEY ? "Cash deposit" : "Cash withdrawal");
    int transaction_id = 0, passbook_id = 0;
    enum Transaction_Type type = usecase == DEPOSIT_MONEY ? DEPOSIT : WITHDRAWAL;
    int history_status = service_append_transaction(account.accountID, type, amount, description, 0, TRANSACTION_SUCCESS, &transaction_id);
    if (history_status == 0) history_status = service_append_passbook(account.accountID, transaction_id, description,
                                    usecase == WITHDRAW_MONEY ? amount : 0.0f,
                                    usecase == DEPOSIT_MONEY ? amount : 0.0f, balance, &passbook_id);
    if (history_status != 0) {
        /* Tombstone any partially appended history and reverse the balance update. */
        if (passbook_id > 0) db_tombstone_record(DB_PASSBOOK, sizeof(PassbookEntry), offsetof(PassbookEntry, entryID), offsetof(PassbookEntry, is_active), passbook_id);
        if (transaction_id > 0) db_tombstone_record(DB_TRANSACTIONS, sizeof(Transaction), offsetof(Transaction, transactionID), offsetof(Transaction, is_active), transaction_id);
        service_make_account_adjustment(account_index, -delta, NULL);
        service_error(output, "The transaction could not be recorded; the balance update was rolled back where possible.");
        return;
    }
    service_add_log(description, "Customer balance updated");
    output->response.balance = balance;
    service_success(output, usecase == DEPOSIT_MONEY ? "Deposit successful." : "Withdrawal successful.");
}

/* Transfer between two active accounts. The server serializes requests and updates
   both complete account records before appending debit and credit history rows. */
static void customer_transfer(const ServiceRequest *request, ServiceOutput *output)
{
    Customer customer;
    if (current_customer(&customer, NULL) != 0) { service_error(output, "Customer profile was not found."); return; }
    const TransferRequest *transfer = &request->transfer;
    if (transfer->source_account_number <= 0 || transfer->recipient_account_number <= 0 ||
        transfer->source_account_number == transfer->recipient_account_number ||
        !(transfer->amount > 0.0f) || !isfinite(transfer->amount)) {
        service_error(output, "Enter two different valid account numbers and a positive amount."); return;
    }

    BankAccount source, destination;
    off_t source_index, destination_index;
    if (service_find_customer_account_number(transfer->source_account_number, &source, &source_index) != 0 ||
        !service_account_belongs_to_customer(&source, customer.CustomerID)) { service_error(output, "Source account does not belong to this customer."); return; }
    if (service_find_customer_account_number(transfer->recipient_account_number, &destination, &destination_index) != 0) { service_error(output, "Recipient account was not found."); return; }
    if (db_find_customer(destination.customerID, NULL, NULL) != 0) { service_error(output, "Recipient customer is not active."); return; }

    float source_balance = 0.0f, destination_balance = 0.0f;
    int source_status = service_make_account_adjustment(source_index, -transfer->amount, &source_balance);
    if (source_status == 1) { service_error(output, "Source account is inactive or has insufficient funds."); return; }
    if (source_status != 0) { service_error(output, "Could not update the source account."); return; }
    int destination_status = service_make_account_adjustment(destination_index, transfer->amount, &destination_balance);
    if (destination_status != 0) {
        service_make_account_adjustment(source_index, transfer->amount, NULL);
        service_error(output, "Recipient account is inactive; source balance was restored where possible.");
        return;
    }

    int debit_transaction = 0, credit_transaction = 0, debit_entry = 0, credit_entry = 0;
    int ok = service_append_transaction(source.accountID, TRANSFER, transfer->amount, "Funds transferred out", destination.accountID, TRANSACTION_SUCCESS, &debit_transaction);
    if (ok == 0) ok = service_append_transaction(destination.accountID, TRANSFER, transfer->amount, "Funds transferred in", source.accountID, TRANSACTION_SUCCESS, &credit_transaction);
    if (ok == 0) ok = service_append_passbook(source.accountID, debit_transaction, "Transfer debit", transfer->amount, 0.0f, source_balance, &debit_entry);
    if (ok == 0) ok = service_append_passbook(destination.accountID, credit_transaction, "Transfer credit", 0.0f, transfer->amount, destination_balance, &credit_entry);
    if (ok != 0) {
        /* Remove partial history rows from active views, then compensate both balances. */
        if (debit_entry > 0) db_tombstone_record(DB_PASSBOOK, sizeof(PassbookEntry), offsetof(PassbookEntry, entryID), offsetof(PassbookEntry, is_active), debit_entry);
        if (credit_entry > 0) db_tombstone_record(DB_PASSBOOK, sizeof(PassbookEntry), offsetof(PassbookEntry, entryID), offsetof(PassbookEntry, is_active), credit_entry);
        if (debit_transaction > 0) db_tombstone_record(DB_TRANSACTIONS, sizeof(Transaction), offsetof(Transaction, transactionID), offsetof(Transaction, is_active), debit_transaction);
        if (credit_transaction > 0) db_tombstone_record(DB_TRANSACTIONS, sizeof(Transaction), offsetof(Transaction, transactionID), offsetof(Transaction, is_active), credit_transaction);
        service_make_account_adjustment(source_index, transfer->amount, NULL);
        service_make_account_adjustment(destination_index, -transfer->amount, NULL);
        service_error(output, "Transfer history could not be stored; balances were rolled back where possible.");
        return;
    }

    char notification[256];
    snprintf(notification, sizeof(notification), "A transfer of %.2f was credited to account %ld.", transfer->amount, destination.account_number);
    service_add_notification(destination.customerID, notification);
    service_add_log("TRANSFER_FUNDS", "Transfer debited one account and credited another");
    output->response.balance = source_balance;
    service_success(output, "Transfer successful.");
}

/* Add a new customer loan request; loan-type limits are read from loan_type.txt. */
static void customer_apply_loan(const ServiceRequest *request, ServiceOutput *output)
{
    Customer customer;
    if (current_customer(&customer, NULL) != 0) { service_error(output, "Customer profile was not found."); return; }
    const ApplyLoanRequest *input = &request->apply_loan;
    LoanType type;
    if (!(input->amount > 0.0f) || !isfinite(input->amount) || input->tenure_months <= 0 || db_find_loan_type(input->loan_type, &type, NULL) != 0) {
        service_error(output, "Invalid loan type, amount, or tenure."); return;
    }
    if (input->amount > type.maxAmount || input->tenure_months > type.max_tenure_months) {
        service_error(output, "Requested amount or tenure exceeds the loan type limit."); return;
    }
    LoanApplication application = {0};
    if (service_next_id(DB_LOAN_APPLICATIONS, sizeof(application), offsetof(LoanApplication, applicationID), &application.applicationID) == -1) { service_error(output, "Could not allocate an application ID."); return; }
    application.customerID = customer.CustomerID;
    application.loan_type = input->loan_type;
    application.amount_requested = input->amount;
    application.status = LOAN_PENDING;
    application.applied_date = time(NULL);
    application.processed_by = 0;
    application.tenure_months = input->tenure_months;
    application.is_active = true;
    if (db_append_record(DB_LOAN_APPLICATIONS, sizeof(application), &application, NULL) == -1) { service_error(output, "Could not save the loan application."); return; }
    char details[128];
    snprintf(details, sizeof(details), "Loan application %d created", application.applicationID);
    service_add_log("APPLY_LOAN", details);
    output->response.record_id = application.applicationID;
    service_success(output, "Loan application submitted successfully.");
}

/* Add a customer feedback record for a manager to review. */
static void customer_add_feedback(const ServiceRequest *request, ServiceOutput *output)
{
    Customer customer;
    if (current_customer(&customer, NULL) != 0) { service_error(output, "Customer profile was not found."); return; }
    if (request->feedback.message[0] == '\0') { service_error(output, "Feedback cannot be empty."); return; }
    Feedback row = {0};
    if (service_next_id(DB_FEEDBACK, sizeof(row), offsetof(Feedback, feedbackID), &row.feedbackID) == -1) { service_error(output, "Could not allocate feedback ID."); return; }
    row.customerID = customer.CustomerID;
    snprintf(row.message, sizeof(row.message), "%s", request->feedback.message);
    row.date_time = time(NULL);
    row.status = FEEDBACK_OPEN;
    row.is_active = true;
    if (db_append_record(DB_FEEDBACK, sizeof(row), &row, NULL) == -1) { service_error(output, "Could not save feedback."); return; }
    service_add_log("ADD_FEEDBACK", "Customer submitted feedback");
    output->response.record_id = row.feedbackID;
    service_success(output, "Feedback submitted successfully.");
}

/* Implement the customer's allowed use cases. Anything else is explicitly rejected. */
void customer_main(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output)
{
    Customer customer;
    switch (usecase) {
        case VIEW_ACCOUNT_BALANCE: {
            if (current_customer(&customer, NULL) != 0) { service_error(output, "Customer profile was not found."); break; }
            BankAccount accounts[MAX_LIST_RECORDS];
            int count = service_get_customer_accounts(customer.CustomerID, accounts, NULL, MAX_LIST_RECORDS);
            if (count < 0) { service_error(output, "Could not read the account file."); break; }
            float total = 0.0f;
            for (int i = 0; i < count; i++) if (accounts[i].account_status == ACTIVE) total += accounts[i].balance;
            output->response.balance = total;
            service_success(output, "Account balance retrieved.");
            break;
        }
        case DEPOSIT_MONEY:
        case WITHDRAW_MONEY:
            customer_money(usecase, request, output);
            break;
        case TRANSFER_FUNDS:
            customer_transfer(request, output);
            break;
        case APPLY_LOAN:
            customer_apply_loan(request, output);
            break;
        case TRANSACTION_HISTORY:
            if (current_customer(&customer, NULL) != 0 || service_collect_passbook(customer.CustomerID, output) == -1) service_error(output, "Could not read this customer's transaction history.");
            else service_success(output, "Transaction history follows.");
            break;
        case CHANGE_PASSWORD:
            service_change_password(request->change_password.new_password, output);
            break;
        case ADD_FEEDBACK:
            customer_add_feedback(request, output);
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
