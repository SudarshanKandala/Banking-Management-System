#ifndef LOGIN_H
#define LOGIN_H
#include <stdio.h>
#include <stdbool.h>
#include "../include/enums.h"

int login(int sock_fd, enum User_Role role);

#endif