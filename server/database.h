#ifndef DATABASE_H
#define DATABASE_H

#include <stddef.h>
#include <sys/types.h>
#include "../include/types.h"

#define DB_USERS "database/user.txt"
#define DB_CUSTOMERS "database/customer.txt"
#define DB_EMPLOYEES "database/bank_employee.txt"
#define DB_MANAGERS "database/manager.txt"
#define DB_ADMINISTRATORS "database/administrator.txt"
#define DB_ACCOUNTS "database/bank_account.txt"
#define DB_PASSBOOK "database/passbook.txt"
#define DB_LOAN_TYPES "database/loan_type.txt"
#define DB_LOAN_APPLICATIONS "database/loan_application.txt"
#define DB_LOANS "database/loan.txt"
#define DB_TRANSACTIONS "database/transaction.txt"
#define DB_FEEDBACK "database/feedback.txt"
#define DB_SYSTEM_LOGS "database/system_log.txt"
#define DB_NOTIFICATIONS "database/notification.txt"
#define DB_SETTINGS "database/configuration.txt"
#define DB_LOAN_PAYMENTS "database/loan_payment.txt"

typedef int (*db_mutator_fn)(void *record, void *context);

/* This mutex serializes threads in this server; fcntl locks coordinate file access. */
void db_lock(void);
void db_unlock(void);
int db_initialize_files(void);
int db_count_records(const char *path, size_t record_size, off_t *count);
int db_read_record(const char *path, size_t record_size, off_t index, void *record);
int db_append_record(const char *path, size_t record_size, const void *record, off_t *new_index);
int db_modify_record(const char *path, size_t record_size, off_t index, db_mutator_fn mutator, void *context);
int db_next_id(const char *path, size_t record_size, size_t id_offset, int *next_id);
int db_tombstone_record(const char *path, size_t record_size, size_t id_offset, size_t active_offset, int id);
int db_find_user_by_username(const char *username, enum User_Role role, User *result, off_t *index);
int db_find_user_by_id(int user_id, User *result, off_t *index);
int db_find_customer(int customer_id, Customer *result, off_t *index);
int db_find_employee(int employee_id, BankEmployee *result, off_t *index);
int db_find_account_by_number(long int account_number, BankAccount *result, off_t *index);
int db_find_loan_type(enum Loan_Type loan_type, LoanType *result, off_t *index);
int db_find_loan_application(int application_id, LoanApplication *result, off_t *index);
int db_append_log(int user_id, const char *action, const char *details);
int db_append_notification(int user_id, const char *message);

#endif
