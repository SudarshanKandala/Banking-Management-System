#include "client_common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../socket_comm/socket_io.h"
#include "../include/config.h"

/* Read one line and remove its trailing newline. All input helpers use fgets. */
void client_read_line(const char *prompt, char *buffer, size_t size)
{
    if (size == 0) return;
    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, (int)size, stdin) == NULL) {
        /* EOF means the interactive client should stop instead of spinning in input loops. */
        exit(EXIT_SUCCESS);
    }

    size_t length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') {
        buffer[length - 1] = '\0';
    } else {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) { }
    }
}

/* Read and validate an integer without leaving junk in stdin. */
int client_read_int(const char *prompt)
{
    char line[128];
    for (;;) {
        client_read_line(prompt, line, sizeof(line));
        if (line[0] == '\0') continue;
        char *end = NULL;
        errno = 0;
        long value = strtol(line, &end, 10);
        if (errno == 0 && end != line && *end == '\0' && value >= -2147483647L - 1 && value <= 2147483647L) return (int)value;
        printf("Enter a valid integer.\n");
    }
}

/* Read and validate a long integer such as an account number. */
long int client_read_long(const char *prompt)
{
    char line[128];
    for (;;) {
        client_read_line(prompt, line, sizeof(line));
        if (line[0] == '\0') continue;
        char *end = NULL;
        errno = 0;
        long value = strtol(line, &end, 10);
        if (errno == 0 && end != line && *end == '\0') return value;
        printf("Enter a valid whole number.\n");
    }
}

/* Read and validate a floating-point amount. The server checks business rules too. */
float client_read_float(const char *prompt)
{
    char line[128];
    for (;;) {
        client_read_line(prompt, line, sizeof(line));
        if (line[0] == '\0') continue;
        char *end = NULL;
        errno = 0;
        float value = strtof(line, &end);
        if (errno == 0 && end != line && *end == '\0') return value;
        printf("Enter a valid amount.\n");
    }
}

/* Print a timestamp without assuming that ctime always succeeds. */
static void print_time(time_t value)
{
    const char *text = ctime(&value);
    if (text != NULL) printf("%s", text);
    else printf("(time unavailable)\n");
}

/* Receive and display exactly the records listed in ServiceResponse. */
static int receive_and_print_records(int sock_fd, const ServiceResponse *response)
{
    if (response->record_count < 0 || response->record_count > MAX_LIST_RECORDS) {
        fprintf(stderr, "Server returned an invalid record count.\n");
        return -1;
    }

    for (int i = 0; i < response->record_count; i++) {
        switch (response->list_kind) {
            case RESPONSE_LIST_PASSBOOK: {
                PassbookEntry row = {0};
                if (recv_all(sock_fd, &row, sizeof(row)) != (ssize_t)sizeof(row)) return -1;
                printf("\nPassbook entry #%d\n", row.entryID);
                printf("Account ID: %d | Transaction ID: %d\n", row.accountID, row.transactionID);
                printf("Date: "); print_time(row.date_time);
                printf("Particulars: %s\nDebit: %.2f | Credit: %.2f | Balance after: %.2f\n",
                       row.particulars, row.debit, row.credit, row.balance_after);
                break;
            }
            case RESPONSE_LIST_LOAN_APPLICATIONS: {
                LoanApplication row = {0};
                if (recv_all(sock_fd, &row, sizeof(row)) != (ssize_t)sizeof(row)) return -1;
                printf("\nLoan application #%d\n", row.applicationID);
                printf("Customer ID: %d | Type: %d | Amount: %.2f | Tenure: %d months\n",
                       row.customerID, row.loan_type, row.amount_requested, row.tenure_months);
                printf("Status: %d | Assigned employee ID: %d\nApplied: ", row.status, row.processed_by);
                print_time(row.applied_date);
                break;
            }
            case RESPONSE_LIST_FEEDBACK: {
                Feedback row = {0};
                if (recv_all(sock_fd, &row, sizeof(row)) != (ssize_t)sizeof(row)) return -1;
                printf("\nFeedback #%d (customer %d, status %d)\n", row.feedbackID, row.customerID, row.status);
                printf("Date: "); print_time(row.date_time);
                printf("%s\n", row.message);
                break;
            }
            case RESPONSE_LIST_SYSTEM_LOGS: {
                SystemLog row = {0};
                if (recv_all(sock_fd, &row, sizeof(row)) != (ssize_t)sizeof(row)) return -1;
                printf("\nLog #%d | User ID: %d\n", row.logID, row.userID);
                printf("Action: %s\nDetails: %s\nDate: ", row.action, row.details);
                print_time(row.date_time);
                break;
            }
            case RESPONSE_LIST_SETTINGS: {
                Configuration row = {0};
                if (recv_all(sock_fd, &row, sizeof(row)) != (ssize_t)sizeof(row)) return -1;
                printf("\nSetting #%d\nKey: %s\nValue: %s\nDescription: %s\n",
                       row.configID, row.key, row.value, row.description);
                break;
            }
            default:
                fprintf(stderr, "Server returned an unknown list type.\n");
                return -1;
        }
    }
    if (response->record_count == 0 && response->list_kind != RESPONSE_LIST_NONE) printf("No records to display.\n");
    return 0;
}

/* Use the common wire format shared with server/user.c for every role and use case. */
int client_service_request(int sock_fd, enum User_Role role, enum Usecases usecase,
                           const ServiceRequest *request, ServiceResponse *response)
{
    RequestHeader header = {0};
    ServiceRequest empty_request = {0};
    header.role = role;
    header.Use_case = usecase;
    if (request == NULL) request = &empty_request;

    if (send_all(sock_fd, &header, sizeof(header)) != (ssize_t)sizeof(header)) {
        perror("Sending service header");
        return -1;
    }
    /* The server always reads a full ServiceRequest union after the header. */
    if (send_all(sock_fd, request, sizeof(*request)) != (ssize_t)sizeof(*request)) {
        perror("Sending service request");
        return -1;
    }
    if (recv_all(sock_fd, response, sizeof(*response)) != (ssize_t)sizeof(*response)) {
        perror("Receiving service response");
        return -1;
    }

    response->message[sizeof(response->message) - 1] = '\0';
    printf("\nServer: %s\n", response->message[0] ? response->message : "Response received.");
    if (receive_and_print_records(sock_fd, response) == -1) {
        perror("Receiving response records");
        return -1;
    }

    /* An expired session or self-deactivation requires a new login on a new connection. */
    if (strstr(response->message, "Session is not active") != NULL ||
        strstr(response->message, "Please log in again") != NULL ||
        strstr(response->message, "session has been invalidated") != NULL) return 1;
    return 0;
}
