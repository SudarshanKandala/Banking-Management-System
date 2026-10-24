#include "services.h"
#include "service_common.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "login_check.h"

/* Tombstone a login row if creation of its mandatory role profile fails. */
static int tombstone_new_user(void *record, void *context)
{
    User *user = record;
    (void)context;
    user->is_active = false;
    user->isActive = false;
    return 0;
}

/* Return 1 if any active user already uses this username. */
static int admin_username_exists(const char *username)
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

/* Administrators can create a bank employee's login and profile together. */
static void admin_add_employee(const AddEmployeeRequest *input, ServiceOutput *output)
{
    if (input->username[0] == '\0' || input->password[0] == '\0' || input->name[0] == '\0') {
        service_error(output, "Username, password, and employee name are required."); return;
    }
    int exists = admin_username_exists(input->username);
    if (exists != 0) { service_error(output, exists < 0 ? "Could not check usernames." : "That username already exists."); return; }

    User user = {0};
    if (service_next_id(DB_USERS, sizeof(user), offsetof(User, userID), &user.userID) == -1) { service_error(output, "Could not allocate user ID."); return; }
    snprintf(user.username, sizeof(user.username), "%s", input->username);
    if (service_hash_password(input->password, user.passwordHash) == -1) { service_error(output, "Could not hash the initial password."); return; }
    user.Role = BANK_EMPLOYEE;
    user.isActive = true;
    user.is_active = true;

    BankEmployee employee = {0};
    employee.employeeID = user.userID;
    snprintf(employee.name, sizeof(employee.name), "%s", input->name);
    snprintf(employee.department, sizeof(employee.department), "%s", input->department);
    employee.branchID = input->branchID;
    employee.is_active = true;
    off_t user_index;
    if (db_append_record(DB_USERS, sizeof(user), &user, &user_index) == -1) {
        service_error(output, "Could not save the employee login record."); return;
    }
    if (db_append_record(DB_EMPLOYEES, sizeof(employee), &employee, NULL) == -1) {
        db_modify_record(DB_USERS, sizeof(User), user_index, tombstone_new_user, NULL);
        service_error(output, "Employee profile could not be saved; the login record was tombstoned."); return;
    }
    char details[128];
    snprintf(details, sizeof(details), "Bank employee userID=%d created", user.userID);
    service_add_log("ADD_NEW_BANK_EMPLOYEE", details);
    output->response.record_id = user.userID;
    service_success(output, "Bank employee created successfully.");
}

typedef struct { ModifyPersonRequest input; } AdminModifyContext;
static int admin_modify_customer(void *record, void *context)
{
    Customer *customer = record;
    AdminModifyContext *change = context;
    if (!customer->is_active || customer->CustomerID != change->input.target_id) return 1;
    snprintf(customer->name, sizeof(customer->name), "%s", change->input.name);
    snprintf(customer->email, sizeof(customer->email), "%s", change->input.email);
    customer->phone = change->input.phone;
    snprintf(customer->address, sizeof(customer->address), "%s", change->input.address);
    return 0;
}

static int admin_modify_employee(void *record, void *context)
{
    BankEmployee *employee = record;
    AdminModifyContext *change = context;
    if (!employee->is_active || employee->employeeID != change->input.target_id) return 1;
    snprintf(employee->name, sizeof(employee->name), "%s", change->input.name);
    snprintf(employee->department, sizeof(employee->department), "%s", change->input.department);
    employee->branchID = change->input.branchID;
    return 0;
}

/* Admins can update customer or employee profiles; clients cannot select other roles here. */
static void admin_modify_person(const ModifyPersonRequest *input, ServiceOutput *output)
{
    AdminModifyContext context = {0};
    context.input = *input;
    int status = 1;
    if (input->target_role == CUSTOMER) {
        Customer item;
        off_t index;
        if (db_find_customer(input->target_id, &item, &index) == 0) status = db_modify_record(DB_CUSTOMERS, sizeof(Customer), index, admin_modify_customer, &context);
    } else if (input->target_role == BANK_EMPLOYEE) {
        BankEmployee item;
        off_t index;
        if (db_find_employee(input->target_id, &item, &index) == 0) status = db_modify_record(DB_EMPLOYEES, sizeof(BankEmployee), index, admin_modify_employee, &context);
    } else {
        service_error(output, "Administrators can modify customer or bank employee profiles through this service."); return;
    }
    if (status != 0) { service_error(output, "Profile was not found or could not be modified."); return; }
    service_add_log("MODIFY_CUSTOMER_DETAILS", "Administrator modified a customer or employee profile");
    service_success(output, "Profile modified successfully.");
}

typedef struct { int user_id; enum User_Role new_role; int activate; int delete_record; } UserManagementContext;
static int modify_user_record(void *record, void *context)
{
    User *user = record;
    UserManagementContext *change = context;
    if (!user->is_active || user->userID != change->user_id) return 1;
    if (change->delete_record) { user->is_active = false; user->isActive = false; }
    else { user->Role = change->new_role; user->isActive = change->activate != 0; }
    return 0;
}

typedef struct { size_t active_offset; } ActiveFlagContext;
static int tombstone_profile(void *record, void *context)
{
    ActiveFlagContext *flag = context;
    *(bool *)((unsigned char *)record + flag->active_offset) = false;
    return 0;
}

/* Mark a role-specific profile as deleted without physically removing its slot. */
static void tombstone_current_profile(enum User_Role role, int user_id)
{
    off_t count;
    const char *path = NULL;
    size_t size = 0, id_offset = 0, active_offset = 0;
    switch (role) {
        case CUSTOMER: path = DB_CUSTOMERS; size = sizeof(Customer); id_offset = offsetof(Customer, CustomerID); active_offset = offsetof(Customer, is_active); break;
        case BANK_EMPLOYEE: path = DB_EMPLOYEES; size = sizeof(BankEmployee); id_offset = offsetof(BankEmployee, employeeID); active_offset = offsetof(BankEmployee, is_active); break;
        case BANK_MANAGER: path = DB_MANAGERS; size = sizeof(BankManager); id_offset = offsetof(BankManager, managerID); active_offset = offsetof(BankManager, is_active); break;
        case ADMINISTRATOR: path = DB_ADMINISTRATORS; size = sizeof(Administrator); id_offset = offsetof(Administrator, adminID); active_offset = offsetof(Administrator, is_active); break;
        default: return;
    }
    if (db_count_records(path, size, &count) == -1) return;
    for (off_t i = 0; i < count; i++) {
        unsigned char record[sizeof(User) > sizeof(Customer) ? sizeof(User) : sizeof(Customer)];
        /* Other profiles are smaller than this buffer in the current types.h. */
        if (db_read_record(path, size, i, record) != 0) return;
        int id;
        bool active;
        memcpy(&id, record + id_offset, sizeof(id));
        memcpy(&active, record + active_offset, sizeof(active));
        if (id == user_id && active) { ActiveFlagContext flag = {active_offset}; db_modify_record(path, size, i, tombstone_profile, &flag); return; }
    }
}

/* Create a minimal role profile when an administrator changes a user's role. */
static int create_minimal_profile(enum User_Role role, int user_id, const char *name)
{
    switch (role) {
        case CUSTOMER: {
            Customer row = {0}; row.CustomerID = user_id; snprintf(row.name, sizeof(row.name), "%s", name); row.is_active = true;
            return db_append_record(DB_CUSTOMERS, sizeof(row), &row, NULL);
        }
        case BANK_EMPLOYEE: {
            BankEmployee row = {0}; row.employeeID = user_id; snprintf(row.name, sizeof(row.name), "%s", name); row.is_active = true;
            return db_append_record(DB_EMPLOYEES, sizeof(row), &row, NULL);
        }
        case BANK_MANAGER: {
            BankManager row = {0}; row.managerID = user_id; snprintf(row.name, sizeof(row.name), "%s", name); row.is_active = true;
            return db_append_record(DB_MANAGERS, sizeof(row), &row, NULL);
        }
        case ADMINISTRATOR: {
            Administrator row = {0}; row.adminID = user_id; snprintf(row.name, sizeof(row.name), "%s", name); row.is_active = true;
            return db_append_record(DB_ADMINISTRATORS, sizeof(row), &row, NULL);
        }
        default: return -1;
    }
}

/* Manage users by soft deletion, activation/deactivation, or role reassignment. */
static void admin_manage_user(const ManageUserRequest *input, ServiceOutput *output)
{
    int actor_id = get_current_user_id();
    User user;
    off_t index;
    if (db_find_user_by_id(input->userID, &user, &index) != 0) { service_error(output, "User was not found."); return; }
    if (input->action == ADD) { service_error(output, "Use ADD_NEW_BANK_EMPLOYEE to create an employee; other role profiles must be provisioned separately."); return; }
    if (input->action != MODIFY && input->action != DELETE) { service_error(output, "Invalid user-management action."); return; }

    UserManagementContext context = {input->userID, input->new_role, input->activate, input->action == DELETE};
    if (input->action == MODIFY && (input->new_role < CUSTOMER || input->new_role > ADMINISTRATOR)) { service_error(output, "Invalid role."); return; }
    bool role_changed = input->action == MODIFY && user.Role != input->new_role;
    if (role_changed && create_minimal_profile(input->new_role, user.userID, user.username) == -1) {
        service_error(output, "Could not create the new role profile."); return;
    }
    if (db_modify_record(DB_USERS, sizeof(User), index, modify_user_record, &context) != 0) {
        /* A newly created profile must not remain active if the User row update fails. */
        if (role_changed) tombstone_current_profile(input->new_role, user.userID);
        service_error(output, "User could not be updated."); return;
    }
    if (role_changed || input->action == DELETE) tombstone_current_profile(user.Role, user.userID);
    service_add_log("MANAGE_USER", input->action == DELETE ? "Administrator soft-deleted a user" : "Administrator changed user status or role");
    invalidate_user_sessions(user.userID);
    if (user.userID == actor_id) {
        output->end_session = 1;
        service_success(output, "User updated. Your session has been invalidated.");
    } else {
        service_success(output, input->action == DELETE ? "User deactivated using a tombstone; history was retained." : "User updated successfully.");
    }
}

typedef struct { SettingRequest input; } SettingContext;
static int update_setting(void *record, void *context)
{
    Configuration *setting = record;
    SettingContext *change = context;
    if (!setting->is_active || strcmp(setting->key, change->input.key) != 0) return 1;
    snprintf(setting->value, sizeof(setting->value), "%s", change->input.value);
    snprintf(setting->description, sizeof(setting->description), "%s", change->input.description);
    return 0;
}

/* List system settings when key is empty; otherwise insert/update one named key. */
static void admin_system_settings(const SettingRequest *input, ServiceOutput *output)
{
    off_t count;
    if (db_count_records(DB_SETTINGS, sizeof(Configuration), &count) == -1) { service_error(output, "Could not read settings file."); return; }
    if (input->key[0] == '\0') {
        output->response.list_kind = RESPONSE_LIST_SETTINGS;
        output->response.record_count = 0;
        for (off_t i = 0; i < count && output->response.record_count < MAX_LIST_RECORDS; i++) {
            Configuration row;
            if (db_read_record(DB_SETTINGS, sizeof(row), i, &row) != 0) { service_error(output, "Could not read settings file."); return; }
            if (row.is_active) output->settings[output->response.record_count++] = row;
        }
        service_success(output, "System settings follow.");
        return;
    }

    off_t found_index = -1;
    for (off_t i = 0; i < count; i++) {
        Configuration row;
        if (db_read_record(DB_SETTINGS, sizeof(row), i, &row) != 0) { service_error(output, "Could not read settings file."); return; }
        if (row.is_active && strcmp(row.key, input->key) == 0) { found_index = i; break; }
    }
    if (found_index >= 0) {
        SettingContext context = {0}; context.input = *input;
        if (db_modify_record(DB_SETTINGS, sizeof(Configuration), found_index, update_setting, &context) != 0) { service_error(output, "Setting could not be updated."); return; }
    } else {
        Configuration row = {0};
        if (service_next_id(DB_SETTINGS, sizeof(row), offsetof(Configuration, configID), &row.configID) == -1) { service_error(output, "Could not allocate setting ID."); return; }
        snprintf(row.key, sizeof(row.key), "%s", input->key);
        snprintf(row.value, sizeof(row.value), "%s", input->value);
        snprintf(row.description, sizeof(row.description), "%s", input->description);
        row.is_active = true;
        if (db_append_record(DB_SETTINGS, sizeof(row), &row, NULL) == -1) { service_error(output, "Setting could not be saved."); return; }
    }
    service_add_log("SYSTEM_SETTINGS", "Administrator inserted or updated a setting");
    service_success(output, "System setting saved.");
}

/* The system log is returned as a bounded list of complete SystemLog records. */
static void admin_view_logs(ServiceOutput *output)
{
    off_t count;
    if (db_count_records(DB_SYSTEM_LOGS, sizeof(SystemLog), &count) == -1) { service_error(output, "Could not read system logs."); return; }
    output->response.list_kind = RESPONSE_LIST_SYSTEM_LOGS;
    output->response.record_count = 0;
    for (off_t i = 0; i < count && output->response.record_count < MAX_LIST_RECORDS; i++) {
        SystemLog row;
        if (db_read_record(DB_SYSTEM_LOGS, sizeof(row), i, &row) != 0) { service_error(output, "Could not read system logs."); return; }
        if (row.is_active) output->logs[output->response.record_count++] = row;
    }
    service_success(output, "System log records follow.");
}

/* Administrator service entry point. */
void administrator_main(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output)
{
    switch (usecase) {
        case CHANGE_PASSWORD:
            service_change_password(request->change_password.new_password, output);
            break;
        case ADD_NEW_BANK_EMPLOYEE:
            admin_add_employee(&request->add_employee, output);
            break;
        case MODIFY_CUSTOMER_DETAILS:
            admin_modify_person(&request->modify_person, output);
            break;
        case MANAGE_USER:
            admin_manage_user(&request->manage_user, output);
            break;
        case VIEW_SYSTEM_LOGS:
            admin_view_logs(output);
            break;
        case SYSTEM_SETTINGS:
            admin_system_settings(&request->setting, output);
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
