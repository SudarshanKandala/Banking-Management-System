#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "login.h"
#include "../include/comm.h"
#include "../socket_comm/socket_io.h"

int login(int sockfd, enum User_Role role){
    printf("Login:\n");
    printf("Enter Username: ");
    char username[128];
    scanf("%s", username);
    printf("Enter Password: ");
    char password[128];
    scanf("%s", password);
    LoginRequest login_details;
    strcpy(login_details.username, username);
    strcpy(login_details.password, password);
    RequestHeader header = {
        .role = role,
        .Use_case = LOGIN,
        .message_size = sizeof(login_details)
    };
    if(send_all(sockfd, &header, sizeof(header)) == -1){
        perror("sending LoginID, password from client to server failed\n");
        return false;
    }
    if(send_all(sockfd, &login_details, sizeof(login_details)) == -1){
        perror("sending LoginID, password from client to server failed\n");
        return false;
    }
    LoginResponse response;
    if(recv_all(sockfd, &response, sizeof(response)) == -1){
        perror("Something Went wrong in receiving server authentication\n");
        exit(EXIT_FAILURE);
    }
    return response.success;
}