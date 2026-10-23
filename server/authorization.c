#include <stdio.h>
#include <stdbool.h>
#include "../include/comm.h"
#include "../include/enums.h"
#include "login_check.h"

bool authorize(RequestHeader header){
    switch(header.role){
        case CUSTOMER:
            
        case BANK_EMPLOYEE:
            return true;
        case BANK_MANAGER:
            return true;
        case ADMINISTRATOR:
            return true;
    }
}