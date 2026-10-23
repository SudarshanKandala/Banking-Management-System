#ifndef COMM_H
#define COMM_H

#include "types.h"
#include <stdio.h>

// server client Communication

typedef struct{
    enum User_Role role;
    enum Usecases Use_case;
    ssize_t message_size;
} RequestHeader;

typedef struct{
    char username[128];
    char password[128];
} LoginRequest;

typedef struct{
    int success; 
} LoginResponse;

typedef struct{
    long int recipient_account;
    float amount;
} TransferRequest;

typedef struct{
    float amount;
} DepositRequest, WithdrawRequest;

typedef struct{
    char new_password[128];
} ChangePasswordRequest;

typedef struct{
    char feedback[256];
} FeedbackRequest;

typedef struct{
    enum Loan_Type loan_type;
    float amount;
    int tenure_months;
} LoanRequest;


#endif