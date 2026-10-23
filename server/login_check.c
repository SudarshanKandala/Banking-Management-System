#include <stdio.h>
#include <stdbool.h>
#include "login_check.h"

bool is_session_active(RequestHeader header, LoginRequest login_details){
    
}

int customer_login(RequestHeader header, LoginRequest login_details){
    if(is_session_active(header, login_details)) return 2;
    return 0;
}

int bank_employee_login(RequestHeader header, LoginRequest login_details){
    if(is_session_active(header, login_details)) return 2;
    return 0;
}

int bank_manager_login(RequestHeader header, LoginRequest login_details){
    if(is_session_active(header, login_details)) return 2;
    return 0;
}

int administrator_login(RequestHeader header, LoginRequest login_details){
    if(is_session_active(header, login_details)) return 2;
    return 0;
}

int login_check(RequestHeader header, LoginRequest login_details){
    switch(header.role){
        case CUSTOMER:
            return customer_login(header, login_details);
        case BANK_EMPLOYEE:
            return bank_employee_login(header, login_details);
        case BANK_MANAGER:
            return bank_manager_login(header, login_details);
        case ADMINISTRATOR:
            return administrator_login(header, login_details);
        default:
            return -1;
    }
}