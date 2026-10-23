#include <stdio.h>
#include <stdlib.h>
#include "user.h"
#include "../socket_comm/socket_io.h"
#include "../include/enums.h"
#include "../include/comm.h"
#include "../include/types.h"


void user_main(int sock_fd){
    // Receive Header
    RequestHeader header;
    ssize_t received_bytes = recv_all(sock_fd, &header, sizeof(header));
    if(received_bytes == -1){
        printf("Something Went wrong in receiving header");
        return;
    }
    if(received_bytes == 0){
        printf("Connection closed by client");
        return;
    }
    if(received_bytes != sizeof(header)){
        printf("Something Went wrong in header");
        return;
    }
    // Receive Body
}