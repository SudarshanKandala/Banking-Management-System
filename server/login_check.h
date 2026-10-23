#ifndef LOGIN_CHECK_H
#define LOGIN_CHECK_H
#include <stdbool.h>
#include "../include/comm.h"

int login_check(RequestHeader header, LoginRequest login_details);
bool is_session_active(RequestHeader header, LoginRequest login_details);

#endif