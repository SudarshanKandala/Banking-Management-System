#ifndef TYPES_H
#define TYPES_H
#include "enums.h"
// User and Access Management

typedef struct{
    int userID;
    char username[128];
    char passwordHash[128];
    enum User_Role Role;
    bool isActive;
    time_t last_login;
    bool is_active; /* true when this database/session record is valid */
} User;

typedef struct{
    int CustomerID;
    char name[128];
    char email[128];
    int phone;
    char address[256];
    bool is_active; /* true when this database/session record is valid */
} Customer;

typedef struct{
    int employeeID;
    char name[128];
    char department[128];
    int branchID;
    bool is_active; /* true when this database/session record is valid */
} BankEmployee;

typedef struct{
    int managerID;
    char name[128];
    char department[128];
    bool is_active; /* true when this database/session record is valid */
} BankManager;

typedef struct{
    int adminID;
    char name[128];
    bool is_active; /* true when this database/session record is valid */
} Administrator;

typedef struct{
    int sessionID;
    int userID;
    time_t login_time;
    time_t last_access_time;
    bool isActive;
    int expiryDuration;
    bool is_active; /* true when this database/session record is valid */
} Session;

// Banking Domain

typedef struct{
    int bankID;
    char name[128];
    char headOffice[128];
    char contactInfo[128];
    bool is_active; /* true when this database/session record is valid */
} Bank;

typedef struct{
    int branchID;
    char name[128];
    char address[128];
    char ifscCode[128];
    bool is_active; /* true when this database/session record is valid */
} Branch;

typedef struct{
    enum Loan_Type loan_type;
    char name[128];
    char description[256];
    double interest_rate;
    float maxAmount;
    int max_tenure_months;
    bool is_active; /* true when this database/session record is valid */
} LoanType;

typedef struct{
    int applicationID;
    int customerID;
    enum Loan_Type loan_type;
    float amount_requested;
    enum Loan_Status status;
    time_t applied_date;
    int processed_by;
    int tenure_months;
    bool is_active; /* true when this database/session record is valid */
} LoanApplication;

typedef struct{
    int loanID;
    int applicationID;
    int customerID;
    int loanTypeID;
    float amount;
    double interest_rate;
    int tenure_months;
    time_t start_date;
    enum Loan_Status status;
    bool is_active; /* true when this database/session record is valid */
} Loan;

typedef struct{
    int paymentID;
    int loanID;
    float amount;
    time_t payment_date;
    enum Payment_Mode mode;
    enum Payment_Status status;
    bool is_active; /* true when this database/session record is valid */
} LoanPayment;

// Transactions and Accounts

typedef struct{
    int accountID;
    long int account_number;
    enum Account_Type account_type;
    float balance;
    enum Account_Status account_status;
    int branchID;
    int customerID;
    time_t opened_date;
    bool is_active; /* true when this database/session record is valid */
} BankAccount;

typedef struct{
    int transactionID;
    int accountID;
    enum Transaction_Type type;
    float amount;
    time_t transaction_time;
    char description[256];
    int reference_accountID;
    enum Transaction_Status status;
    bool is_active; /* true when this database/session record is valid */
} Transaction;

typedef struct{
    int entryID;
    int accountID;
    int transactionID;
    time_t date_time;
    char particulars[256];
    float debit;
    float credit;
    float balance_after;
    bool is_active; /* true when this database/session record is valid */
} PassbookEntry;

// System and Utility Classes

typedef struct{
    int feedbackID;
    int customerID;
    char message[256];
    time_t date_time;
    enum FeedBack_Status status;
    bool is_active; /* true when this database/session record is valid */
} Feedback;

typedef struct{
    int logID;
    int userID;
    char action[128];
    char details[256];
    time_t date_time;
    bool is_active; /* true when this database/session record is valid */
} SystemLog;

typedef struct{
    int notificationID;
    int userID;
    char message[256];
    time_t date_time;
    bool isRead;
    bool is_active; /* true when this database/session record is valid */
} Notification;

typedef struct{
    int configID;
    char key[128];
    char value[128];
    char description[256];
    bool is_active; /* true when this database/session record is valid */
} Configuration;    

#endif
