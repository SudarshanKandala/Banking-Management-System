#define _POSIX_C_SOURCE 200809L
#include "database.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* One server process uses threads; this mutex is needed because classic F_SETLKW
   record locks are process-associated and do not by themselves serialize threads. */
static pthread_mutex_t database_mutex = PTHREAD_MUTEX_INITIALIZER;

void db_lock(void) { pthread_mutex_lock(&database_mutex); }
void db_unlock(void) { pthread_mutex_unlock(&database_mutex); }

static int open_database_file(const char *path)
{
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd == -1) perror(path);
    return fd;
}

/* Acquire or release a POSIX advisory byte-range lock. */
static int lock_range(int fd, short type, off_t start, off_t length)
{
    struct flock lock = {0};
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    lock.l_start = start;
    lock.l_len = length;
    int command = type == F_UNLCK ? F_SETLK : F_SETLKW;
    while (fcntl(fd, command, &lock) == -1) {
        if (errno == EINTR && type != F_UNLCK) continue;
        return -1;
    }
    return 0;
}

/* Database .txt rows are fixed-size ASCII hex encodings of zero-initialized C
   records. This permits a stable byte offset for fcntl locking and in-place
   replacement, while avoiding embedded NUL bytes in a text file. */
static size_t record_slot_size(size_t record_size) { return (record_size * 2U) + 1U; }

static char hex_digit(unsigned int value) { return "0123456789abcdef"[value & 15U]; }

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void encode_record(const unsigned char *record, size_t record_size, char *slot)
{
    for (size_t i = 0; i < record_size; i++) {
        slot[2U * i] = hex_digit(record[i] >> 4);
        slot[2U * i + 1U] = hex_digit(record[i]);
    }
    slot[2U * record_size] = '\n';
}

static int decode_record(const char *slot, size_t record_size, unsigned char *record)
{
    for (size_t i = 0; i < record_size; i++) {
        int high = hex_value(slot[2U * i]);
        int low = hex_value(slot[2U * i + 1U]);
        if (high < 0 || low < 0) return -1;
        record[i] = (unsigned char)((high << 4) | low);
    }
    return slot[2U * record_size] == '\n' ? 0 : -1;
}

/* Create all required data files if they do not exist yet. */
int db_initialize_files(void)
{
    const char *paths[] = {
        DB_USERS, DB_CUSTOMERS, DB_EMPLOYEES, DB_MANAGERS,
        DB_ADMINISTRATORS, DB_ACCOUNTS, DB_PASSBOOK, DB_LOAN_TYPES,
        DB_LOAN_APPLICATIONS, DB_LOANS, DB_TRANSACTIONS, DB_FEEDBACK,
        DB_SYSTEM_LOGS, DB_NOTIFICATIONS, DB_SETTINGS, DB_LOAN_PAYMENTS
    };
    if (mkdir("database", 0700) == -1 && errno != EEXIST) { perror("mkdir database"); return -1; }
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        int fd = open(paths[i], O_RDWR | O_CREAT, 0600);
        if (fd == -1) { perror(paths[i]); return -1; }
        close(fd);
    }
    return 0;
}

/* Count the fixed-size slots in a data file under a whole-file read lock. */
int db_count_records(const char *path, size_t record_size, off_t *count)
{
    if (!path || record_size == 0 || !count) return -1;
    int fd = open_database_file(path);
    if (fd == -1) return -1;
    struct stat st;
    int result = -1;
    if (lock_range(fd, F_RDLCK, 0, 0) == -1) { perror("fcntl read lock"); goto done; }
    if (fstat(fd, &st) == -1) { perror("fstat database"); goto unlock; }
    size_t slot_size = record_slot_size(record_size);
    if ((uintmax_t)st.st_size % slot_size != 0) {
        fprintf(stderr, "Database file %s has an incomplete record slot\n", path);
        goto unlock;
    }
    *count = st.st_size / (off_t)slot_size;
    result = 0;
unlock:
    lock_range(fd, F_UNLCK, 0, 0);
done:
    close(fd);
    return result;
}

/* Read one record while holding a shared lock on its exact text slot. */
int db_read_record(const char *path, size_t record_size, off_t index, void *record)
{
    if (!path || !record || index < 0 || record_size == 0) return -1;
    int fd = open_database_file(path);
    if (fd == -1) return -1;
    size_t slot_size = record_slot_size(record_size);
    off_t offset = index * (off_t)slot_size;
    char *slot = malloc(slot_size);
    if (!slot) { close(fd); return -1; }
    int result = -1;
    if (lock_range(fd, F_RDLCK, offset, (off_t)slot_size) == -1) { perror("fcntl record read lock"); goto done; }
    ssize_t n = pread(fd, slot, slot_size, offset);
    if (n == 0) result = 1; /* No record at this index. */
    else if (n != (ssize_t)slot_size) { errno = EIO; perror("read database record"); }
    else if (decode_record(slot, record_size, record) == -1) fprintf(stderr, "Corrupt fixed-slot record in %s at index %lld\n", path, (long long)index);
    else result = 0;
    lock_range(fd, F_UNLCK, offset, (off_t)slot_size);
done:
    free(slot);
    close(fd);
    return result;
}

/* Append one record while holding a whole-file write lock to serialize appenders. */
int db_append_record(const char *path, size_t record_size, const void *record, off_t *new_index)
{
    if (!path || !record || record_size == 0) return -1;
    int fd = open_database_file(path);
    if (fd == -1) return -1;
    size_t slot_size = record_slot_size(record_size);
    char *slot = malloc(slot_size);
    if (!slot) { close(fd); return -1; }
    int result = -1;
    if (lock_range(fd, F_WRLCK, 0, 0) == -1) { perror("fcntl append lock"); goto done; }
    struct stat st;
    if (fstat(fd, &st) == -1) { perror("fstat append"); goto unlock; }
    if ((uintmax_t)st.st_size % slot_size != 0) { fprintf(stderr, "Misaligned append position in %s\n", path); goto unlock; }
    encode_record(record, record_size, slot);
    size_t written = 0;
    while (written < slot_size) {
        ssize_t n = pwrite(fd, slot + written, slot_size - written, st.st_size + (off_t)written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { perror("append database record"); goto unlock; }
        written += (size_t)n;
    }
    if (new_index) *new_index = st.st_size / (off_t)slot_size;
    result = 0;
unlock:
    lock_range(fd, F_UNLCK, 0, 0);
done:
    free(slot);
    close(fd);
    return result;
}

/* Read, mutate, and rewrite a complete record while its exact slot is locked. */
int db_modify_record(const char *path, size_t record_size, off_t index, db_mutator_fn mutator, void *context)
{
    if (!path || !mutator || index < 0 || record_size == 0) return -1;
    int fd = open_database_file(path);
    if (fd == -1) return -1;
    size_t slot_size = record_slot_size(record_size);
    off_t offset = index * (off_t)slot_size;
    unsigned char *record = malloc(record_size);
    char *slot = malloc(slot_size);
    if (!record || !slot) { free(record); free(slot); close(fd); return -1; }
    int result = -1;
    if (lock_range(fd, F_WRLCK, offset, (off_t)slot_size) == -1) { perror("fcntl record write lock"); goto done; }
    ssize_t n = pread(fd, slot, slot_size, offset);
    if (n != (ssize_t)slot_size) { errno = EIO; perror("read record before update"); goto unlock; }
    if (decode_record(slot, record_size, record) == -1) { fprintf(stderr, "Corrupt record before update in %s\n", path); goto unlock; }
    int mutation = mutator(record, context);
    if (mutation != 0) { result = mutation; goto unlock; }
    encode_record(record, record_size, slot);
    size_t written = 0;
    while (written < slot_size) {
        ssize_t m = pwrite(fd, slot + written, slot_size - written, offset + (off_t)written);
        if (m < 0 && errno == EINTR) continue;
        if (m <= 0) { perror("write modified database record"); goto unlock; }
        written += (size_t)m;
    }
    result = 0;
unlock:
    lock_range(fd, F_UNLCK, offset, (off_t)slot_size);
done:
    free(record);
    free(slot);
    close(fd);
    return result;
}

/* Scan an ID field and return one greater than the largest ID, including tombstones. */
int db_next_id(const char *path, size_t record_size, size_t id_offset, int *next_id)
{
    if (!next_id || id_offset + sizeof(int) > record_size) return -1;
    off_t count;
    if (db_count_records(path, record_size, &count) == -1) return -1;
    int highest = 0;
    unsigned char *record = malloc(record_size);
    if (!record) return -1;
    for (off_t i = 0; i < count; i++) {
        int status = db_read_record(path, record_size, i, record);
        if (status != 0) { free(record); return -1; }
        int id;
        memcpy(&id, record + id_offset, sizeof(id));
        if (id > highest) highest = id;
    }
    free(record);
    *next_id = highest + 1;
    return 0;
}

typedef struct { size_t id_offset; size_t active_offset; int id; } TombstoneContext;
static int tombstone_record_mutator(void *record, void *context)
{
    TombstoneContext *target = context;
    int id;
    memcpy(&id, (unsigned char *)record + target->id_offset, sizeof(id));
    if (id != target->id) return 1;
    *(bool *)((unsigned char *)record + target->active_offset) = false;
    return 0;
}

/* Soft-delete a record by ID without removing or shifting any later record slots. */
int db_tombstone_record(const char *path, size_t record_size, size_t id_offset, size_t active_offset, int id)
{
    if (id_offset + sizeof(int) > record_size || active_offset + sizeof(bool) > record_size) return -1;
    off_t count;
    if (db_count_records(path, record_size, &count) == -1) return -1;
    unsigned char *record = malloc(record_size);
    if (!record) return -1;
    int result = 1;
    for (off_t i = 0; i < count; i++) {
        if (db_read_record(path, record_size, i, record) != 0) { result = -1; break; }
        int found_id;
        bool active;
        memcpy(&found_id, record + id_offset, sizeof(found_id));
        memcpy(&active, record + active_offset, sizeof(active));
        if (found_id == id && active) {
            TombstoneContext context = {id_offset, active_offset, id};
            result = db_modify_record(path, record_size, i, tombstone_record_mutator, &context);
            break;
        }
    }
    free(record);
    return result == 0 ? 0 : -1;
}

int db_find_user_by_username(const char *username, enum User_Role role, User *result, off_t *index)
{
    off_t count;
    if (db_count_records(DB_USERS, sizeof(User), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        User item;
        if (db_read_record(DB_USERS, sizeof(item), i, &item) != 0) return -1;
        if (item.is_active && item.isActive && item.Role == role && strcmp(item.username, username) == 0) {
            if (result) *result = item;
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

int db_find_user_by_id(int user_id, User *result, off_t *index)
{
    off_t count;
    if (db_count_records(DB_USERS, sizeof(User), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        User item;
        if (db_read_record(DB_USERS, sizeof(item), i, &item) != 0) return -1;
        if (item.is_active && item.userID == user_id) {
            if (result) *result = item;
            if (index) *index = i;
            return 0;
        }
    }
    return 1;
}

int db_find_customer(int customer_id, Customer *result, off_t *index)
{
    off_t count;
    if (db_count_records(DB_CUSTOMERS, sizeof(Customer), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        Customer item;
        if (db_read_record(DB_CUSTOMERS, sizeof(item), i, &item) != 0) return -1;
        if (item.is_active && item.CustomerID == customer_id) { if (result) *result = item; if (index) *index = i; return 0; }
    }
    return 1;
}

int db_find_employee(int employee_id, BankEmployee *result, off_t *index)
{
    off_t count;
    if (db_count_records(DB_EMPLOYEES, sizeof(BankEmployee), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        BankEmployee item;
        if (db_read_record(DB_EMPLOYEES, sizeof(item), i, &item) != 0) return -1;
        if (item.is_active && item.employeeID == employee_id) { if (result) *result = item; if (index) *index = i; return 0; }
    }
    return 1;
}

int db_find_account_by_number(long int account_number, BankAccount *result, off_t *index)
{
    off_t count;
    if (db_count_records(DB_ACCOUNTS, sizeof(BankAccount), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        BankAccount item;
        if (db_read_record(DB_ACCOUNTS, sizeof(item), i, &item) != 0) return -1;
        if (item.is_active && item.account_number == account_number) { if (result) *result = item; if (index) *index = i; return 0; }
    }
    return 1;
}

int db_find_loan_type(enum Loan_Type loan_type, LoanType *result, off_t *index)
{
    off_t count;
    if (db_count_records(DB_LOAN_TYPES, sizeof(LoanType), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        LoanType item;
        if (db_read_record(DB_LOAN_TYPES, sizeof(item), i, &item) != 0) return -1;
        if (item.is_active && item.loan_type == loan_type) { if (result) *result = item; if (index) *index = i; return 0; }
    }
    return 1;
}

int db_find_loan_application(int application_id, LoanApplication *result, off_t *index)
{
    off_t count;
    if (db_count_records(DB_LOAN_APPLICATIONS, sizeof(LoanApplication), &count) == -1) return -1;
    for (off_t i = 0; i < count; i++) {
        LoanApplication item;
        if (db_read_record(DB_LOAN_APPLICATIONS, sizeof(item), i, &item) != 0) return -1;
        if (item.is_active && item.applicationID == application_id) { if (result) *result = item; if (index) *index = i; return 0; }
    }
    return 1;
}

/* Append an audit-log row for a completed service. */
int db_append_log(int user_id, const char *action, const char *details)
{
    SystemLog row = {0};
    if (db_next_id(DB_SYSTEM_LOGS, sizeof(row), offsetof(SystemLog, logID), &row.logID) == -1) return -1;
    row.userID = user_id;
    snprintf(row.action, sizeof(row.action), "%s", action ? action : "unknown");
    snprintf(row.details, sizeof(row.details), "%s", details ? details : "");
    row.date_time = time(NULL);
    row.is_active = true;
    return db_append_record(DB_SYSTEM_LOGS, sizeof(row), &row, NULL);
}

/* Append a notification for the target user, for example after a transfer or loan decision. */
int db_append_notification(int user_id, const char *message)
{
    Notification row = {0};
    if (db_next_id(DB_NOTIFICATIONS, sizeof(row), offsetof(Notification, notificationID), &row.notificationID) == -1) return -1;
    row.userID = user_id;
    snprintf(row.message, sizeof(row.message), "%s", message ? message : "");
    row.date_time = time(NULL);
    row.isRead = false;
    row.is_active = true;
    return db_append_record(DB_NOTIFICATIONS, sizeof(row), &row, NULL);
}
