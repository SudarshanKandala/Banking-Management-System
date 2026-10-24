#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>

#include "../include/types.h"
#include "../server/database.h"
#include "../server/password.h"

/* Mark a partially created administrator's login record invalid if profile creation fails. */
static int tombstone_user_record(void *record, void *context)
{
    User *user = record;
    (void)context;
    user->is_active = false;
    user->isActive = false;
    return 0;
}

/* Create the first administrator interactively so no default password is shipped. */
int main(void)
{
    if (db_initialize_files() == -1) return EXIT_FAILURE;
    char username[128] = {0};
    char password[128] = {0};
    char *entered;
    printf("Initial administrator username: ");
    fflush(stdout);
    if (!fgets(username, sizeof(username), stdin)) return EXIT_FAILURE;
    username[strcspn(username, "\n")] = '\0';
    if (username[0] == '\0') { fprintf(stderr, "Username cannot be empty.\n"); return EXIT_FAILURE; }
    entered = getpass("Initial administrator password (input hidden): ");
    if (!entered || entered[0] == '\0' || strlen(entered) >= sizeof(password)) { fprintf(stderr, "Password is empty or too long.\n"); return EXIT_FAILURE; }
    snprintf(password, sizeof(password), "%s", entered);

    db_lock();
    off_t count;
    if (db_count_records(DB_USERS, sizeof(User), &count) == -1) { db_unlock(); return EXIT_FAILURE; }
    for (off_t i = 0; i < count; i++) {
        User existing;
        if (db_read_record(DB_USERS, sizeof(existing), i, &existing) != 0) { db_unlock(); return EXIT_FAILURE; }
        if (existing.is_active && strcmp(existing.username, username) == 0) { fprintf(stderr, "That username already exists.\n"); db_unlock(); return EXIT_FAILURE; }
    }

    User user = {0};
    if (db_next_id(DB_USERS, sizeof(user), offsetof(User, userID), &user.userID) == -1) { db_unlock(); return EXIT_FAILURE; }
    snprintf(user.username, sizeof(user.username), "%s", username);
    if (password_hash(password, user.passwordHash, sizeof(user.passwordHash)) == -1) { db_unlock(); return EXIT_FAILURE; }
    user.Role = ADMINISTRATOR;
    user.isActive = true;
    user.is_active = true;
    user.last_login = 0;

    Administrator admin = {0};
    admin.adminID = user.userID;
    snprintf(admin.name, sizeof(admin.name), "%s", username);
    admin.is_active = true;
    off_t user_index;
    if (db_append_record(DB_USERS, sizeof(user), &user, &user_index) == -1) {
        db_unlock(); fprintf(stderr, "Could not save administrator login record.\n"); return EXIT_FAILURE;
    }
    if (db_append_record(DB_ADMINISTRATORS, sizeof(admin), &admin, NULL) == -1) {
        /* Do not leave a login record that has no Administrator profile. */
        db_modify_record(DB_USERS, sizeof(User), user_index, tombstone_user_record, NULL);
        db_unlock(); fprintf(stderr, "Administrator profile could not be saved; login record was tombstoned.\n"); return EXIT_FAILURE;
    }

    /* Seed default loan type policy rows only when the file is empty. */
    off_t loan_count;
    if (db_count_records(DB_LOAN_TYPES, sizeof(LoanType), &loan_count) == 0 && loan_count == 0) {
        const char *names[] = {"Home Loan", "Car Loan", "Personal Loan", "Education Loan", "Business Loan"};
        const char *descriptions[] = {"Housing finance", "Vehicle finance", "Unsecured personal finance", "Education finance", "Business finance"};
        const double rates[] = {8.5, 9.0, 12.0, 7.5, 11.0};
        const float limits[] = {5000000.0f, 2500000.0f, 1000000.0f, 2000000.0f, 10000000.0f};
        const int tenures[] = {360, 84, 60, 180, 120};
        for (int i = 0; i < 5; i++) {
            LoanType type = {0};
            type.loan_type = (enum Loan_Type)i;
            snprintf(type.name, sizeof(type.name), "%s", names[i]);
            snprintf(type.description, sizeof(type.description), "%s", descriptions[i]);
            type.interest_rate = rates[i];
            type.maxAmount = limits[i];
            type.max_tenure_months = tenures[i];
            type.is_active = true;
            if (db_append_record(DB_LOAN_TYPES, sizeof(type), &type, NULL) == -1) { db_unlock(); return EXIT_FAILURE; }
        }
    }
    db_unlock();
    memset(password, 0, sizeof(password));
    printf("Administrator created. Start the server and log in with this account.\n");
    return EXIT_SUCCESS;
}
