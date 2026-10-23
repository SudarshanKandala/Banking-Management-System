#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include "connect.h"
#include "login.h"
#include "customer_services.h"

int main(){
    int sock_fd = client_connect();
    home_page:
    printf("\n\n$---------$ Welcome to IIITB Bank $---------$\n\n");
    printf("Who are you?\n\n");
    printf("1) Customer\n");
    printf("2) Bank Employee\n");
    printf("3) Bank Manager\n");
    printf("4) Administrator\n");
    printf("5) Exit\n");
    printf("Enter the Number Corresponding to the role: ");
    int role_num;
    scanf("%d", &role_num);
    enum User_Role role;
    if(role_num == 1)
        role = CUSTOMER;
    else if(role_num == 2)
        role = BANK_EMPLOYEE;
    else if(role_num == 3)
        role = BANK_MANAGER;
    else if(role_num == 4)
        role = ADMINISTRATOR;
    else if(role_num == 5){
        close(sock_fd);
        printf("\nThank you for using IIITB Bank Services\n");
        return 0;
    }
    else{
        printf("Enter Correct role number\n");
        goto home_page;
    }

    int login_result = login(sock_fd, role);
    if(login_result == 1){
        printf("Login Failed. Try again...\n");
        goto home_page;
    }
    else if(login_result == 2){
        printf("You are logged in elsewhere.\n");
        goto home_page;
    }
    printf("Login Successful\n");

    switch(role){
        case CUSTOMER:
            customer(sock_fd);
            break;
        case BANK_EMPLOYEE:
            bank_employee(sock_fd);
            break;
        case BANK_MANAGER:
            bank_manager(sock_fd);
            break;
        case ADMINISTRATOR:
            administrator(sock_fd);
            break;
    }
    goto home_page;
}
