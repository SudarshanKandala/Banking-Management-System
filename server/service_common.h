#ifndef SERVICE_COMMON_H
#define SERVICE_COMMON_H

#include "services.h"
#include "database.h"

void service_output_init(ServiceOutput *output);
void service_success(ServiceOutput *output, const char *message);
void service_error(ServiceOutput *output, const char *message);
int service_next_id(const char *path, size_t size, size_t id_offset, int *next_id);
int service_get_current_user(User *user, off_t *index);
int service_change_password(const char *new_password, ServiceOutput *output);
int service_get_customer_accounts(int customer_id, BankAccount *accounts, off_t *indices, int maximum);
int service_collect_passbook(int customer_id, ServiceOutput *output);
int service_append_passbook(int account_id, int transaction_id, const char *particulars, float debit, float credit, float balance_after, int *entry_id);
int service_append_transaction(int account_id, enum Transaction_Type type, float amount, const char *description, int reference_account_id, enum Transaction_Status status, int *transaction_id);
int service_make_account_adjustment(off_t account_index, float amount_delta, float *new_balance);
int service_account_belongs_to_customer(const BankAccount *account, int customer_id);
int service_add_log(const char *action, const char *details);
int service_add_notification(int user_id, const char *message);
int service_find_customer_account_number(long int account_number, BankAccount *account, off_t *index);
int service_find_account_by_id(int account_id, BankAccount *account, off_t *index);
int service_collect_pending_loans(int employee_id, int assigned_only, ServiceOutput *output);
int service_collect_open_feedback(ServiceOutput *output);
int service_find_user_by_id(int user_id, User *user, off_t *index);
int service_hash_password(const char *password, char output[128]);

#endif
