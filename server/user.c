#include "user.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "../include/comm.h"
#include "../socket_comm/socket_io.h"
#include "database.h"
#include "login_check.h"
#include "services.h"

/* Ensure client-provided fixed-size strings are terminated before any strcmp/snprintf. */
static void sanitize_request(enum Usecases usecase, ServiceRequest *request)
{
    switch (usecase) {
        case ADD_NEW_CUSTOMER:
            request->add_customer.username[127] = '\0'; request->add_customer.password[127] = '\0';
            request->add_customer.name[127] = '\0'; request->add_customer.email[127] = '\0'; request->add_customer.address[255] = '\0';
            break;
        case MODIFY_CUSTOMER_DETAILS:
            request->modify_person.name[127] = '\0'; request->modify_person.email[127] = '\0';
            request->modify_person.address[255] = '\0'; request->modify_person.department[127] = '\0';
            break;
        case CHANGE_PASSWORD:
            request->change_password.new_password[127] = '\0';
            break;
        case ADD_FEEDBACK:
            request->feedback.message[255] = '\0';
            break;
        case ADD_NEW_BANK_EMPLOYEE:
            request->add_employee.username[127] = '\0'; request->add_employee.password[127] = '\0';
            request->add_employee.name[127] = '\0'; request->add_employee.department[127] = '\0';
            break;
        case SYSTEM_SETTINGS:
            request->setting.key[127] = '\0'; request->setting.value[127] = '\0'; request->setting.description[255] = '\0';
            break;
        default:
            break;
    }
}

/* Send a response header followed by zero or more fixed-size records. */
static int send_service_output(int sock_fd, const ServiceOutput *output)
{
    if (send_all(sock_fd, &output->response, sizeof(output->response)) != (ssize_t)sizeof(output->response)) return -1;
    for (int i = 0; i < output->response.record_count; i++) {
        const void *record = NULL;
        size_t size = 0;
        switch (output->response.list_kind) {
            case RESPONSE_LIST_PASSBOOK: record = &output->passbook[i]; size = sizeof(PassbookEntry); break;
            case RESPONSE_LIST_LOAN_APPLICATIONS: record = &output->loan_applications[i]; size = sizeof(LoanApplication); break;
            case RESPONSE_LIST_FEEDBACK: record = &output->feedback[i]; size = sizeof(Feedback); break;
            case RESPONSE_LIST_SYSTEM_LOGS: record = &output->logs[i]; size = sizeof(SystemLog); break;
            case RESPONSE_LIST_SETTINGS: record = &output->settings[i]; size = sizeof(Configuration); break;
            default: return -1;
        }
        if (send_all(sock_fd, record, size) != (ssize_t)size) return -1;
    }
    return 0;
}

/* Process all authenticated requests for one client. Only this function receives
   request headers/payloads; role-specific functions process one decoded request. */
void user_main(int sock_fd)
{
    for (;;) {
        RequestHeader header = {0};
        ssize_t received = recv_all(sock_fd, &header, sizeof(header));
        if (received == 0) { logout_current_session(); return; }
        if (received != (ssize_t)sizeof(header)) { perror("receiving request header"); logout_current_session(); return; }

        /* Session validity is checked before reading/processing the request body. */
        if (!is_session_active()) {
            logout_current_session();
            ServiceOutput expired;
            service_output_init(&expired);
            service_error(&expired, "Session is not active. Please log in again.");
            send_service_output(sock_fd, &expired);
            shutdown(sock_fd, SHUT_RDWR);
            return;
        }

        if (header.Use_case == LOGIN) {
            ServiceOutput invalid;
            service_output_init(&invalid);
            service_error(&invalid, "You are already authenticated on this connection.");
            send_service_output(sock_fd, &invalid);
            shutdown(sock_fd, SHUT_RDWR);
            logout_current_session();
            return;
        }

        ServiceRequest request = {0};
        received = recv_all(sock_fd, &request, sizeof(request));
        if (received == 0) { logout_current_session(); return; }
        if (received != (ssize_t)sizeof(request)) { perror("receiving request payload"); logout_current_session(); return; }
        sanitize_request(header.Use_case, &request);

        ServiceOutput output;
        service_output_init(&output);
        /* Recheck the current User row: an administrator may have disabled the user
           or changed their role while this connection was idle. */
        db_lock();
        User authenticated_user;
        int current_user_id = get_current_user_id();
        int user_found = current_user_id >= 0 ? db_find_user_by_id(current_user_id, &authenticated_user, NULL) : 1;
        if (user_found != 0 || !authenticated_user.isActive || authenticated_user.Role != get_current_user_role()) {
            db_unlock();
            logout_current_session();
            service_error(&output, "Your account is inactive or its role changed. Please log in again.");
            send_service_output(sock_fd, &output);
            shutdown(sock_fd, SHUT_RDWR);
            return;
        }
        if (header.role != authenticated_user.Role) {
            service_error(&output, "Request role does not match the authenticated user's role.");
        } else {
            /* Keep related file operations serialized within this threaded server. The
               database layer adds fcntl byte-range locks for cross-process file access. */
            switch (authenticated_user.Role) {
                case CUSTOMER: customer_main(header.Use_case, &request, &output); break;
                case BANK_EMPLOYEE: bank_employee_main(header.Use_case, &request, &output); break;
                case BANK_MANAGER: bank_manager_main(header.Use_case, &request, &output); break;
                case ADMINISTRATOR: administrator_main(header.Use_case, &request, &output); break;
                default: service_error(&output, "You are not allowed to use that service."); break;
            }
        }
        db_unlock();

        if (send_service_output(sock_fd, &output) == -1) { perror("sending service response"); logout_current_session(); return; }
        if (output.end_session) { logout_current_session(); return; }
    }
}
