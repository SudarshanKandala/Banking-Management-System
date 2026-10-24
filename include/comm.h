#ifndef COMM_H
#define COMM_H

#include "types.h"

/* Every request begins with this header. LOGIN is followed by LoginRequest;
   every authenticated request is followed by a fixed-size ServiceRequest. */
typedef struct {
    enum User_Role role;
    enum Usecases Use_case;
} RequestHeader;

typedef struct {
    char username[128];
    char password[128];
} LoginRequest;

/* success is an integer: 0=success, 1=authentication failed, 2=already logged in. */
typedef struct {
    int success;
    int session_id;
    char message[128];
} LoginResponse;

typedef struct {
    char username[128];
    char password[128];
    char name[128];
    char email[128];
    int phone;
    char address[256];
} AddCustomerRequest;

typedef struct {
    enum User_Role target_role;
    int target_id;
    char name[128];
    char email[128];
    int phone;
    char address[256];
    char department[128];
    int branchID;
} ModifyPersonRequest;

typedef struct {
    int customerID;
    enum Account_Type account_type;
    int branchID;
    float initial_deposit;
} OpenAccountRequest;

typedef struct {
    long int account_number;
    float amount;
} MoneyRequest;

typedef struct {
    long int source_account_number;
    long int recipient_account_number;
    float amount;
} TransferRequest;

typedef struct {
    enum Loan_Type loan_type;
    float amount;
    int tenure_months;
} ApplyLoanRequest;

typedef struct {
    char new_password[128];
} ChangePasswordRequest;

typedef struct {
    char message[256];
} FeedbackRequest;

typedef struct {
    int applicationID;
    int approve; /* 1=approve, 0=reject */
} DecideLoanRequest;

typedef struct {
    int customerID;
} CustomerLookupRequest;

typedef struct {
    long int account_number;
    enum Account_Status account_status;
} SetAccountStatusRequest;

typedef struct {
    int applicationID;
    int employeeID;
} AssignLoanRequest;

typedef struct {
    int feedbackID; /* 0 means list open feedback */
    enum FeedBack_Status status;
} ReviewFeedbackRequest;

typedef struct {
    int userID;
    enum Action action;
    enum User_Role new_role;
    int activate; /* 1=activate, 0=deactivate; ignored for DELETE */
} ManageUserRequest;

typedef struct {
    char key[128]; /* empty key means list settings */
    char value[128];
    char description[256];
} SettingRequest;

typedef struct {
    char username[128];
    char password[128];
    char name[128];
    char department[128];
    int branchID;
} AddEmployeeRequest;

/* Fixed-size request payload keeps the TCP protocol simple and unambiguous. */
typedef union {
    AddCustomerRequest add_customer;
    ModifyPersonRequest modify_person;
    OpenAccountRequest open_account;
    MoneyRequest money;
    TransferRequest transfer;
    ApplyLoanRequest apply_loan;
    ChangePasswordRequest change_password;
    FeedbackRequest feedback;
    DecideLoanRequest decide_loan;
    CustomerLookupRequest customer_lookup;
    SetAccountStatusRequest account_status;
    AssignLoanRequest assign_loan;
    ReviewFeedbackRequest review_feedback;
    ManageUserRequest manage_user;
    SettingRequest setting;
    AddEmployeeRequest add_employee;
} ServiceRequest;

enum ResponseListKind {
    RESPONSE_LIST_NONE,
    RESPONSE_LIST_PASSBOOK,
    RESPONSE_LIST_LOAN_APPLICATIONS,
    RESPONSE_LIST_FEEDBACK,
    RESPONSE_LIST_SYSTEM_LOGS,
    RESPONSE_LIST_SETTINGS
};

/* Sent before any optional list records. All list records follow as raw structs. */
typedef struct {
    int status; /* 0=success, nonzero=failure */
    int record_count;
    int list_kind;
    int record_id;
    float balance;
    char message[256];
} ServiceResponse;

#endif
