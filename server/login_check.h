#ifndef LOGIN_CHECK_H
#define LOGIN_CHECK_H

#include <stdbool.h>
#include "../include/comm.h"
#include "../include/types.h"

#define MAX_SESSIONS 100
#define SESSION_TIMEOUT_SECONDS 300

typedef struct {
    int next_session_id;
    Session sessions[MAX_SESSIONS];
} SharedSessionTable;

extern SharedSessionTable *session_table;
extern int session_shmid;

int initialize_session_store(void);
void destroy_session_store(void);
int login_check(RequestHeader header, LoginRequest login_details);
bool is_session_active(void);
int get_current_session_id(void);
int get_current_user_id(void);
enum User_Role get_current_user_role(void);
void logout_current_session(void);
void invalidate_user_sessions(int user_id);

#endif
