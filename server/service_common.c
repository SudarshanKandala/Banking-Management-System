#include "service_common.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "password.h"
#include "login_check.h"

/* Initialize a response and its in-memory list buffers before dispatch. */
void service_output_init(ServiceOutput *output)
{
    memset(output, 0, sizeof(*output));
    output->response.status = 0;
    output->response.list_kind = RESPONSE_LIST_NONE;
}

void service_success(ServiceOutput *output, const char *message)
{
    output->response.status = 0;
    snprintf(output->response.message, sizeof(output->response.message), "%s", message ? message : "Success");
}

void service_error(ServiceOutput *output, const char *message)
{
    output->response.status = 1;
    output->response.record_count = 0;
    output->response.list_kind = RESPONSE_LIST_NONE;
    snprintf(output->response.message, sizeof(output->response.message), "%s", message ? message : "Request failed");
}

int service_next_id(const char *path, size_t size, size_t id_offset, int *next_id)
{
    return db_next_id(path, size, id_offset, next_id);
}

/* Resolve the user attached to the current authenticated thread/session. */
int service_get_current_user(User *user, off_t *index)
{
    int user_id = get_current_user_id();
    if (user_id < 0) return -1;
    return db_find_user_by_id(user_id, user, index);
}

/* Hash a password before storing it in User.passwordHash. */
int service_hash_password(const char *password, char output[128])
{
    return password_hash(password, output, 128);
}

typedef struct { char hash[128]; } PasswordContext;
static int replace_password(void *record, void *context)
{
    User *user = record;
    PasswordContext *replacement = context;
    if (!user->is_active || !user->isActive) return 1;
    snprintf(user->passwordHash, sizeof(user->passwordHash), "%s", replacement->hash);
    return 0;
}

/* Change password only for the currently logged-in user. */
int service_change_password(const char *new_password, ServiceOutput *output)
{
    if (!new_password || new_password[0] == '\0') { service_error(output, "Password cannot be empty."); return -1; }
    User user;
    off_t index;
    if (service_get_current_user(&user, &index) != 0) { service_error(output, "Current user record was not found."); return -1; }
    PasswordContext context = {0};
    if (password_hash(new_password, context.hash, sizeof(context.hash)) == -1) { service_error(output, "Could not securely hash the password."); return -1; }
    int status = db_modify_record(DB_USERS, sizeof(User), index, replace_password, &context);
    if (status != 0) { service_error(output, "Password change failed."); return -1; }
    service_add_log("CHANGE_PASSWORD", "User changed their password");
    service_success(output, "Password changed successfully.");
    return 0;
}

int service_account_belongs_to_customer(const BankAccount *account, int customer_id)
{
    return account->is_active && account->customerID == customer_id;
}

/* Collect a customer's active accounts in file order. */
int service_get_customer_accounts(int customer_id, BankAccount *accounts, off_t *indices, int maximum)
{
    off_t count;
    if (db_count_records(DB_ACCOUNTS, sizeof(BankAccount), &count) == -1) return -1;
    int found = 0;
    for (off_t i = 0; i < count && found < maximum; i++) {
        BankAccount row;
        if (db_read_record(DB_ACCOUNTS, sizeof(row), i, &row) != 0) return -1;
        if (row.is_active && row.customerID == customer_id) { accounts[found] = row; if (indices) indices[found] = i; found++; }
    }
    return found;
}

/* Return the first live matching account, regardless of its open/closed status. */
int service_find_account_by_id(int account_id, BankAccount *account, off_t *index)
{
    off_t count;
    if (db_count_records(DB_ACCOUNTS, sizeof(BankAccount), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        BankAccount row;
        if (db_read_record(DB_ACCOUNTS, sizeof(row), i, &row) != 0) return -1;
        if (row.is_active && row.accountID == account_id) { if (account) *account = row; if (index) *index = i; return 0; }
    }
    return 1;
}

int service_find_customer_account_number(long int account_number, BankAccount *account, off_t *index)
{
    return db_find_account_by_number(account_number, account, index);
}

/* Append a passbook row for a debit or credit, then preserve the new balance. */
int service_append_passbook(int account_id, int transaction_id, const char *particulars, float debit, float credit, float balance_after, int *entry_id)
{
    PassbookEntry row = {0};
    if (service_next_id(DB_PASSBOOK, sizeof(row), offsetof(PassbookEntry, entryID), &row.entryID) == -1) return -1;
    row.accountID = account_id;
    row.transactionID = transaction_id;
    row.date_time = time(NULL);
    snprintf(row.particulars, sizeof(row.particulars), "%s", particulars ? particulars : "Transaction");
    row.debit = debit;
    row.credit = credit;
    row.balance_after = balance_after;
    row.is_active = true;
    int result = db_append_record(DB_PASSBOOK, sizeof(row), &row, NULL);
    if (result == 0 && entry_id) *entry_id = row.entryID;
    return result;
}

/* Append one transaction record and return its unique ID. */
int service_append_transaction(int account_id, enum Transaction_Type type, float amount, const char *description, int reference_account_id, enum Transaction_Status status, int *transaction_id)
{
    Transaction row = {0};
    if (service_next_id(DB_TRANSACTIONS, sizeof(row), offsetof(Transaction, transactionID), &row.transactionID) == -1) return -1;
    row.accountID = account_id;
    row.type = type;
    row.amount = amount;
    row.transaction_time = time(NULL);
    snprintf(row.description, sizeof(row.description), "%s", description ? description : "Transaction");
    row.reference_accountID = reference_account_id;
    row.status = status;
    row.is_active = true;
    if (db_append_record(DB_TRANSACTIONS, sizeof(row), &row, NULL) == -1) return -1;
    if (transaction_id) *transaction_id = row.transactionID;
    return 0;
}

typedef struct { float delta; float new_balance; } BalanceContext;
static int adjust_balance(void *record, void *context)
{
    BankAccount *account = record;
    BalanceContext *change = context;
    if (!account->is_active || account->account_status != ACTIVE || account->balance + change->delta < -0.0001f) return 1;
    account->balance += change->delta;
    if (account->balance < 0.0f && account->balance > -0.0001f) account->balance = 0.0f;
    change->new_balance = account->balance;
    return 0;
}

/* Atomically fetch/modify/replace one complete BankAccount slot under its fcntl lock. */
int service_make_account_adjustment(off_t account_index, float amount_delta, float *new_balance)
{
    BalanceContext context = {amount_delta, 0.0f};
    int result = db_modify_record(DB_ACCOUNTS, sizeof(BankAccount), account_index, adjust_balance, &context);
    if (result == 0 && new_balance) *new_balance = context.new_balance;
    return result;
}

/* Append audit log and notification rows using the common file helpers. */
int service_add_log(const char *action, const char *details)
{
    return db_append_log(get_current_user_id(), action, details);
}

int service_add_notification(int user_id, const char *message)
{
    return db_append_notification(user_id, message);
}

/* List the current employee's assigned applications, or all unassigned pending ones. */
int service_collect_pending_loans(int employee_id, int assigned_only, ServiceOutput *output)
{
    off_t count;
    if (db_count_records(DB_LOAN_APPLICATIONS, sizeof(LoanApplication), &count) == -1) return -1;
    output->response.list_kind = RESPONSE_LIST_LOAN_APPLICATIONS;
    output->response.record_count = 0;
    for (off_t i = 0; i < count && output->response.record_count < MAX_LIST_RECORDS; i++) {
        LoanApplication row;
        if (db_read_record(DB_LOAN_APPLICATIONS, sizeof(row), i, &row) != 0) return -1;
        if (!row.is_active || row.status != LOAN_PENDING) continue;
        if (assigned_only ? row.processed_by == employee_id : row.processed_by == 0) output->loan_applications[output->response.record_count++] = row;
    }
    return 0;
}

/* List open customer feedback for managers. */
int service_collect_open_feedback(ServiceOutput *output)
{
    off_t count;
    if (db_count_records(DB_FEEDBACK, sizeof(Feedback), &count) == -1) return -1;
    output->response.list_kind = RESPONSE_LIST_FEEDBACK;
    output->response.record_count = 0;
    for (off_t i = 0; i < count && output->response.record_count < MAX_LIST_RECORDS; i++) {
        Feedback row;
        if (db_read_record(DB_FEEDBACK, sizeof(row), i, &row) != 0) return -1;
        if (row.is_active && row.status == FEEDBACK_OPEN) output->feedback[output->response.record_count++] = row;
    }
    return 0;
}

int service_find_user_by_id(int user_id, User *user, off_t *index)
{
    return db_find_user_by_id(user_id, user, index);
}

/* Collect all active passbook entries for accounts belonging to one customer. */
int service_collect_passbook(int customer_id, ServiceOutput *output)
{
    BankAccount accounts[MAX_LIST_RECORDS];
    int account_count = service_get_customer_accounts(customer_id, accounts, NULL, MAX_LIST_RECORDS);
    if (account_count < 0) return -1;
    off_t count;
    if (db_count_records(DB_PASSBOOK, sizeof(PassbookEntry), &count) == -1) return -1;
    output->response.list_kind = RESPONSE_LIST_PASSBOOK;
    output->response.record_count = 0;
    for (off_t i = 0; i < count && output->response.record_count < MAX_LIST_RECORDS; i++) {
        PassbookEntry row;
        if (db_read_record(DB_PASSBOOK, sizeof(row), i, &row) != 0) return -1;
        if (!row.is_active) continue;
        for (int j = 0; j < account_count; j++) {
            if (accounts[j].accountID == row.accountID) { output->passbook[output->response.record_count++] = row; break; }
        }
    }
    return 0;
}
