#ifndef LOGIN_CHECK_H
#define LOGIN_CHECK_H

#include <stdbool.h>
#include <semaphore.h>
#include "../include/comm.h"
#include "../include/types.h"

#define MAX_SESSIONS 100
#define SESSION_TIMEOUT_SECONDS 300

typedef struct {
    sem_t lock;
    int next_session_id;
    Session sessions[MAX_SESSIONS];
} SharedSessionTable;

extern SharedSessionTable *session_table;
extern int session_shmid;

int initialize_session_store(void);
void destroy_session_store(void);
int login_check(RequestHeader header, LoginRequest login_details);
int customer_login(RequestHeader header, LoginRequest login_details);
int bank_employee_login(RequestHeader header, LoginRequest login_details);
int bank_manager_login(RequestHeader header, LoginRequest login_details);
int administrator_login(RequestHeader header, LoginRequest login_details);
bool is_session_active(void);
int get_current_session_id(void);
int get_current_user_id(void);
void logout_current_session(void);

#endif
