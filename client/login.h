#ifndef LOGIN_H
#define LOGIN_H

#include "../include/enums.h"

/* Returns 0 on success, 1 on failure, and 2 if already logged in elsewhere. */
int login(int sock_fd, enum User_Role role);

#endif
