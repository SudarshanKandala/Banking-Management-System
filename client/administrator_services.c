#include <stdio.h>

#include "administrator_services.h"
#include "client_common.h"
#include "../include/comm.h"
#include "../include/enums.h"

/* Ask for fields supported by the profile modification service. */
static void read_modify_person(ModifyPersonRequest *person)
{
    client_read_line("Name: ", person->name, sizeof(person->name));
    if (person->target_role == CUSTOMER) {
        client_read_line("Email: ", person->email, sizeof(person->email));
        person->phone = client_read_int("Phone number (digits only): ");
        client_read_line("Address: ", person->address, sizeof(person->address));
    } else {
        client_read_line("Department: ", person->department, sizeof(person->department));
        person->branchID = client_read_int("Branch ID: ");
    }
}

/* Present user-management, staff creation, settings, and audit services. */
void administrator(int sock_fd)
{
    for (;;) {
        printf("\n========== ADMINISTRATOR SERVICES ==========\n");
        printf("1) Add a bank employee\n");
        printf("2) Modify a customer or employee profile\n");
        printf("3) Activate/deactivate/change role/delete a user\n");
        printf("4) View system logs\n");
        printf("5) List or add/update system settings\n");
        printf("6) Change password\n");
        printf("7) Logout\n");

        int choice = client_read_int("Select a service: ");
        enum Usecases usecase;
        ServiceRequest request = {0};
        ServiceResponse response = {0};

        switch (choice) {
            case 1:
                usecase = ADD_NEW_BANK_EMPLOYEE;
                client_read_line("Employee username: ", request.add_employee.username, sizeof(request.add_employee.username));
                client_read_line("Initial password: ", request.add_employee.password, sizeof(request.add_employee.password));
                client_read_line("Employee name: ", request.add_employee.name, sizeof(request.add_employee.name));
                client_read_line("Department: ", request.add_employee.department, sizeof(request.add_employee.department));
                request.add_employee.branchID = client_read_int("Branch ID: ");
                break;
            case 2: {
                usecase = MODIFY_CUSTOMER_DETAILS;
                int role = client_read_int("Profile type (0 CUSTOMER, 1 BANK_EMPLOYEE): ");
                if (role != CUSTOMER && role != BANK_EMPLOYEE) {
                    printf("Only customer and bank employee profiles can be modified here.\n");
                    continue;
                }
                request.modify_person.target_role = (enum User_Role)role;
                request.modify_person.target_id = client_read_int("Target profile ID: ");
                read_modify_person(&request.modify_person);
                break;
            }
            case 3: {
                usecase = MANAGE_USER;
                request.manage_user.userID = client_read_int("User ID: ");
                int action = client_read_int("Action (1 MODIFY/activate-deactivate/change role, 2 DELETE): ");
                if (action != MODIFY && action != DELETE) {
                    printf("Choose 1 for MODIFY or 2 for DELETE.\n");
                    continue;
                }
                request.manage_user.action = (enum Action)action;
                if (action == MODIFY) {
                    int role = client_read_int("Role (0 CUSTOMER, 1 BANK_EMPLOYEE, 2 BANK_MANAGER, 3 ADMINISTRATOR): ");
                    if (role < CUSTOMER || role > ADMINISTRATOR) {
                        printf("Invalid role.\n");
                        continue;
                    }
                    request.manage_user.new_role = (enum User_Role)role;
                    request.manage_user.activate = client_read_int("Activate user? (1 yes, 0 no): ") != 0;
                }
                break;
            }
            case 4:
                usecase = VIEW_SYSTEM_LOGS;
                break;
            case 5: {
                usecase = SYSTEM_SETTINGS;
                char key[sizeof(request.setting.key)];
                client_read_line("Setting key (leave empty to list settings): ", key, sizeof(key));
                snprintf(request.setting.key, sizeof(request.setting.key), "%s", key);
                if (request.setting.key[0] != '\0') {
                    client_read_line("Setting value: ", request.setting.value, sizeof(request.setting.value));
                    client_read_line("Description: ", request.setting.description, sizeof(request.setting.description));
                }
                break;
            }
            case 6:
                usecase = CHANGE_PASSWORD;
                client_read_line("New password: ", request.change_password.new_password,
                                 sizeof(request.change_password.new_password));
                break;
            case 7:
                usecase = LOGOUT;
                break;
            default:
                printf("Invalid choice. Select a number from 1 to 7.\n");
                continue;
        }

        int result = client_service_request(sock_fd, ADMINISTRATOR, usecase, &request, &response);
        if (result != 0) return;
        if (response.status == 0 && usecase == ADD_NEW_BANK_EMPLOYEE && response.record_id > 0)
            printf("Created employee ID: %d\n", response.record_id);
        if (usecase == LOGOUT) return;
    }
}
