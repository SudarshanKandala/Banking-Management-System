#include "services.h"
#include "service_common.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "login_check.h"

typedef struct { enum Account_Status status; } AccountStatusContext;
static int change_account_status(void *record, void *context)
{
    BankAccount *account = record;
    AccountStatusContext *change = context;
    if (!account->is_active || account->account_status == CLOSED) return 1;
    account->account_status = change->status;
    return 0;
}

/* Managers may activate, deactivate, or freeze an account but cannot reopen a closed one. */
static void manager_set_account_status(const SetAccountStatusRequest *input, ServiceOutput *output)
{
    if (input->account_status != ACTIVE && input->account_status != INACTIVE && input->account_status != FROZEN) {
        service_error(output, "Choose ACTIVE, INACTIVE, or FROZEN. Closed accounts cannot be reopened."); return;
    }
    BankAccount account;
    off_t index;
    if (service_find_customer_account_number(input->account_number, &account, &index) != 0) { service_error(output, "Account was not found."); return; }
    AccountStatusContext context = {input->account_status};
    int status = db_modify_record(DB_ACCOUNTS, sizeof(BankAccount), index, change_account_status, &context);
    if (status != 0) { service_error(output, "Account status could not be changed."); return; }
    char details[160];
    snprintf(details, sizeof(details), "Account %ld status changed to %d", input->account_number, input->account_status);
    service_add_log("ACTIVATE_DEACTIVATE_CUSTOMER_ACCOUNT", details);
    service_success(output, "Customer account status updated.");
}

typedef struct { int employee_id; } AssignContext;
static int assign_application(void *record, void *context)
{
    LoanApplication *application = record;
    AssignContext *assign = context;
    if (!application->is_active || application->status != LOAN_PENDING || application->processed_by != 0) return 1;
    application->processed_by = assign->employee_id;
    return 0;
}

/* Assign an unassigned pending loan application to an active bank employee. */
static void manager_assign_loan(const AssignLoanRequest *input, ServiceOutput *output)
{
    BankEmployee employee;
    User employee_user;
    if (db_find_employee(input->employeeID, &employee, NULL) != 0 ||
        db_find_user_by_id(input->employeeID, &employee_user, NULL) != 0 ||
        employee_user.Role != BANK_EMPLOYEE || !employee_user.isActive) {
        service_error(output, "Employee does not exist or is inactive."); return;
    }
    LoanApplication application;
    off_t index;
    if (db_find_loan_application(input->applicationID, &application, &index) != 0) { service_error(output, "Loan application was not found."); return; }
    AssignContext context = {input->employeeID};
    int status = db_modify_record(DB_LOAN_APPLICATIONS, sizeof(LoanApplication), index, assign_application, &context);
    if (status != 0) { service_error(output, "Only an unassigned pending application can be assigned."); return; }
    char details[160];
    snprintf(details, sizeof(details), "Loan application %d assigned to employee %d", input->applicationID, input->employeeID);
    service_add_log("ASSIGN_LOAN_APPLICATION", details);
    service_add_notification(input->employeeID, details);
    service_success(output, "Loan application assigned successfully.");
}

typedef struct { enum FeedBack_Status status; } FeedbackContext;
static int update_feedback(void *record, void *context)
{
    Feedback *feedback = record;
    FeedbackContext *change = context;
    if (!feedback->is_active) return 1;
    feedback->status = change->status;
    return 0;
}

/* Managers can list open feedback or mark a selected entry resolved/closed. */
static void manager_review_feedback(const ReviewFeedbackRequest *input, ServiceOutput *output)
{
    if (input->feedbackID == 0) {
        if (service_collect_open_feedback(output) == -1) service_error(output, "Could not read feedback records.");
        else service_success(output, "Open feedback records follow.");
        return;
    }
    if (input->status != FEEDBACK_RESOLVED && input->status != FEEDBACK_CLOSED && input->status != FEEDBACK_OPEN) {
        service_error(output, "Invalid feedback status."); return;
    }
    off_t count;
    if (db_count_records(DB_FEEDBACK, sizeof(Feedback), &count) == -1) { service_error(output, "Could not read feedback file."); return; }
    off_t index = -1;
    for (off_t i = 0; i < count; i++) {
        Feedback row;
        if (db_read_record(DB_FEEDBACK, sizeof(row), i, &row) != 0) { service_error(output, "Could not read feedback file."); return; }
        if (row.is_active && row.feedbackID == input->feedbackID) { index = i; break; }
    }
    if (index == -1) { service_error(output, "Feedback was not found."); return; }
    FeedbackContext context = {input->status};
    if (db_modify_record(DB_FEEDBACK, sizeof(Feedback), index, update_feedback, &context) != 0) { service_error(output, "Feedback status could not be updated."); return; }
    service_add_log("REVIEW_CUSTOMER_FEEDBACK", "Manager updated feedback status");
    service_success(output, "Feedback status updated.");
}

/* Manager service entry point. Unsupported use cases are explicitly rejected. */
void bank_manager_main(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output)
{
    switch (usecase) {
        case CHANGE_PASSWORD:
            service_change_password(request->change_password.new_password, output);
            break;
        case ACTIVATE_DEACTIVATE_CUSTOMER_ACCOUNT:
            manager_set_account_status(&request->account_status, output);
            break;
        case ASSIGN_LOAN_APPLICATION:
            manager_assign_loan(&request->assign_loan, output);
            break;
        case REVIEW_CUSTOMER_FEEDBACK:
            manager_review_feedback(&request->review_feedback, output);
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
