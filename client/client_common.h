#ifndef CLIENT_COMMON_H
#define CLIENT_COMMON_H

#include <stddef.h>
#include "../include/comm.h"

/* Read input consistently so switching between numeric and text fields is safe. */
void client_read_line(const char *prompt, char *buffer, size_t size);
int client_read_int(const char *prompt);
long int client_read_long(const char *prompt);
float client_read_float(const char *prompt);

/* Send a complete service request and consume its response and optional record list.
   Returns 0 for a normal response, 1 when the caller must reconnect/login, or -1 on I/O failure. */
int client_service_request(int sock_fd, enum User_Role role, enum Usecases usecase,
                           const ServiceRequest *request, ServiceResponse *response);

#endif
