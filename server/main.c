#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "connect.h"
#include "../include/comm.h"
#include "../include/enums.h"
#include "../socket_comm/socket_io.h"
#include "login_check.h"
#include "user.h"

void handle_client(int sock_fd){
    RequestHeader header;
    repeat:
    ssize_t received_bytes = recv_all(sock_fd, &header, sizeof(header));
    if(received_bytes == -1){
        perror("Something Went wrong in receiving server authentication\n");
        exit(EXIT_FAILURE);
    }
    if(received_bytes == 0){
        printf("Connection Closed by client\n");
        close(sock_fd);
        return;
    }
    if(received_bytes != sizeof(header)){
        printf("Something Went wrong in header\n");
        goto repeat;
    }
    if(header.Use_case != LOGIN){
        printf("Expected Login Request\n");
        goto repeat;
    }
    if(header.Use_case == LOGIN){
        LoginRequest login_details;
        if(recv_all(sock_fd, &login_details, sizeof(login_details)) == -1){
            perror("Something Went wrong in receiving server authentication\n");
            exit(EXIT_FAILURE);
        }
        LoginResponse response;
        response.success = login_check(header, login_details);
        if(send_all(sock_fd, &response, sizeof(response)) == -1){
            perror("Something Went wrong in sending server authentication\n");
            exit(EXIT_FAILURE);
        }
        if(response.success == 0){
            user_main(sock_fd);
            close(sock_fd);
            return;
        }
    }
    goto repeat;
}

int main(){
    
    server_start(handle_client);
    return 0;
}