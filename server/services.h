#ifndef SERVICES_H
#define SERVICES_H

#include "../include/comm.h"
#include "../include/config.h"

/* Internal response builder. Lists are sent after ServiceResponse as full records. */
typedef struct {
    ServiceResponse response;
    PassbookEntry passbook[MAX_LIST_RECORDS];
    LoanApplication loan_applications[MAX_LIST_RECORDS];
    Feedback feedback[MAX_LIST_RECORDS];
    SystemLog logs[MAX_LIST_RECORDS];
    Configuration settings[MAX_LIST_RECORDS];
    int end_session;
} ServiceOutput;

void service_output_init(ServiceOutput *output);
void service_success(ServiceOutput *output, const char *message);
void service_error(ServiceOutput *output, const char *message);
void customer_main(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output);
void bank_employee_main(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output);
void bank_manager_main(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output);
void administrator_main(enum Usecases usecase, const ServiceRequest *request, ServiceOutput *output);

#endif
