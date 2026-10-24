#define _POSIX_C_SOURCE 200809L
#include "login_check.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <time.h>

#include "database.h"
#include "password.h"

/* The server creates one segment and one semaphore; all client threads share them. */
SharedSessionTable *session_table = NULL;
int session_shmid = -1;
static int session_semid = -1;
static _Thread_local int current_session_id = -1;
static _Thread_local int current_user_id = -1;
static _Thread_local enum User_Role current_user_role = CUSTOMER;

/* union semun is required by semctl on systems where libc does not define it. */
union semun { int val; struct semid_ds *buf; unsigned short *array; struct seminfo *__buf; };

/* Lock or unlock the shared session array using the System V semaphore from class. */
static int session_lock(short operation_value)
{
    if (session_semid == -1) return -1;
    struct sembuf operation = {0, operation_value, 0};
    while (semop(session_semid, &operation, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop session lock");
        return -1;
    }
    return 0;
}

static int lock_sessions(void) { return session_lock(-1); }
static int unlock_sessions(void) { return session_lock(1); }

/* Initialize shared memory and a System V binary semaphore with initial value 1. */
int initialize_session_store(void)
{
    union semun argument;
    session_shmid = shmget(IPC_PRIVATE, sizeof(SharedSessionTable), IPC_CREAT | 0600);
    if (session_shmid == -1) { perror("shmget session table"); return -1; }
    session_table = shmat(session_shmid, NULL, 0);
    if (session_table == (void *)-1) {
        session_table = NULL;
        perror("shmat session table");
        shmctl(session_shmid, IPC_RMID, NULL);
        session_shmid = -1;
        return -1;
    }
    memset(session_table, 0, sizeof(*session_table));
    session_table->next_session_id = 1;

    session_semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    if (session_semid == -1) {
        perror("semget session semaphore");
        shmdt(session_table); session_table = NULL;
        shmctl(session_shmid, IPC_RMID, NULL); session_shmid = -1;
        return -1;
    }
    argument.val = 1;
    if (semctl(session_semid, 0, SETVAL, argument) == -1) {
        perror("semctl SETVAL session semaphore");
        semctl(session_semid, 0, IPC_RMID);
        session_semid = -1;
        shmdt(session_table); session_table = NULL;
        shmctl(session_shmid, IPC_RMID, NULL); session_shmid = -1;
        return -1;
    }
    return 0;
}

/* Clean up shared memory and semaphore when the server is shutting down. */
void destroy_session_store(void)
{
    if (session_semid != -1) { if (semctl(session_semid, 0, IPC_RMID) == -1) perror("semctl IPC_RMID"); session_semid = -1; }
    if (session_table != NULL) { if (shmdt(session_table) == -1) perror("shmdt session table"); session_table = NULL; }
    if (session_shmid != -1) { if (shmctl(session_shmid, IPC_RMID, NULL) == -1) perror("shmctl IPC_RMID"); session_shmid = -1; }
}

static bool expired_locked(Session *session, time_t now)
{
    return !session->isActive || session->expiryDuration <= 0 ||
           difftime(now, session->last_access_time) >= session->expiryDuration;
}

/* Mark timed-out sessions inactive before accepting a new login. */
static void expire_sessions_locked(time_t now)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        Session *session = &session_table->sessions[i];
        if (session->isActive && expired_locked(session, now)) { session->isActive = false; session->is_active = false; }
    }
}

static bool session_id_in_use_locked(int id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) if (session_table->sessions[i].isActive && session_table->sessions[i].sessionID == id) return true;
    return false;
}

/* Add a session only after credentials are verified. The duplicate-login check and
   insertion happen under one semaphore lock, so two simultaneous logins cannot win. */
static int create_session(int user_id)
{
    if (lock_sessions() == -1) return 1;
    time_t now = time(NULL);
    expire_sessions_locked(now);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_table->sessions[i].isActive && session_table->sessions[i].userID == user_id) { unlock_sessions(); return 2; }
    }

    int free_slot = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) if (!session_table->sessions[i].isActive) { free_slot = i; break; }
    if (free_slot == -1) { unlock_sessions(); fprintf(stderr, "Session table is full\n"); return 1; }

    int id = session_table->next_session_id > 0 ? session_table->next_session_id : 1;
    while (session_id_in_use_locked(id)) { id++; if (id <= 0) id = 1; }
    Session fresh = {0};
    fresh.sessionID = id;
    fresh.userID = user_id;
    fresh.login_time = now;
    fresh.last_access_time = now;
    fresh.isActive = true;
    fresh.is_active = true;
    fresh.expiryDuration = SESSION_TIMEOUT_SECONDS;
    session_table->sessions[free_slot] = fresh;
    session_table->next_session_id = id + 1;
    if (session_table->next_session_id <= 0) session_table->next_session_id = 1;
    unlock_sessions();

    current_session_id = id;
    current_user_id = user_id;
    return 0;
}

typedef struct { time_t when; } LoginTimeUpdate;
static int update_login_time(void *record, void *context)
{
    User *user = record;
    LoginTimeUpdate *update = context;
    if (!user->is_active || !user->isActive) return 1;
    user->last_login = update->when;
    return 0;
}

/* Authenticate against user.txt and create a session. Return 0=success, 1=failed,
   2=already logged in elsewhere, matching the existing client convention. */
int login_check(RequestHeader header, LoginRequest login_details)
{
    User user;
    off_t user_index;
    login_details.username[sizeof(login_details.username) - 1] = '\0';
    login_details.password[sizeof(login_details.password) - 1] = '\0';
    current_session_id = -1;
    current_user_id = -1;
    if (header.role < CUSTOMER || header.role > ADMINISTRATOR || login_details.username[0] == '\0' || login_details.password[0] == '\0') return 1;

    db_lock();
    int found = db_find_user_by_username(login_details.username, header.role, &user, &user_index);
    if (found != 0 || !password_verify(login_details.password, user.passwordHash)) { db_unlock(); return 1; }
    /* Require the matching role-specific profile record as well as the User row. */
    bool profile_exists = false;
    if (header.role == CUSTOMER) profile_exists = db_find_customer(user.userID, NULL, NULL) == 0;
    else if (header.role == BANK_EMPLOYEE) profile_exists = db_find_employee(user.userID, NULL, NULL) == 0;
    else if (header.role == BANK_MANAGER) {
        off_t count;
        if (db_count_records(DB_MANAGERS, sizeof(BankManager), &count) == 0) for (off_t i = 0; i < count; i++) {
            BankManager item;
            if (db_read_record(DB_MANAGERS, sizeof(item), i, &item) == 0 && item.is_active && item.managerID == user.userID) { profile_exists = true; break; }
        }
    } else if (header.role == ADMINISTRATOR) {
        off_t count;
        if (db_count_records(DB_ADMINISTRATORS, sizeof(Administrator), &count) == 0) for (off_t i = 0; i < count; i++) {
            Administrator item;
            if (db_read_record(DB_ADMINISTRATORS, sizeof(item), i, &item) == 0 && item.is_active && item.adminID == user.userID) { profile_exists = true; break; }
        }
    }
    if (!profile_exists) { db_unlock(); return 1; }
    int status = create_session(user.userID);
    if (status == 0) {
        current_user_role = user.Role;
        LoginTimeUpdate update = {time(NULL)};
        if (db_modify_record(DB_USERS, sizeof(User), user_index, update_login_time, &update) != 0) {
            logout_current_session();
            status = 1;
        }
    }
    db_unlock();
    return status;
}

/* Check inactivity timeout on each user request and refresh last-access time. */
bool is_session_active(void)
{
    if (current_session_id < 0 || lock_sessions() == -1) return false;
    bool active = false;
    time_t now = time(NULL);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        Session *session = &session_table->sessions[i];
        if (session->isActive && session->sessionID == current_session_id && session->userID == current_user_id) {
            if (expired_locked(session, now)) { session->isActive = false; session->is_active = false; }
            else { session->last_access_time = now; active = true; }
            break;
        }
    }
    unlock_sessions();
    /* Keep the thread-local IDs until logout_current_session() is called so the
       caller can explicitly tombstone the matching shared-memory entry. */
    return active;
}

int get_current_session_id(void) { return current_session_id; }
int get_current_user_id(void) { return is_session_active() ? current_user_id : -1; }
enum User_Role get_current_user_role(void) { return current_user_role; }

/* Logout tombstones the current shared-memory entry so its array slot can be reused. */
void logout_current_session(void)
{
    if (current_session_id >= 0 && session_table != NULL && lock_sessions() == 0) {
        for (int i = 0; i < MAX_SESSIONS; i++) {
            Session *session = &session_table->sessions[i];
            if (session->isActive && session->sessionID == current_session_id) { session->isActive = false; session->is_active = false; break; }
        }
        unlock_sessions();
    }
    current_session_id = -1;
    current_user_id = -1;
}

/* Invalidate every active session for an account changed by the administrator. */
void invalidate_user_sessions(int user_id)
{
    if (session_table == NULL || lock_sessions() == -1) return;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        Session *session = &session_table->sessions[i];
        if (session->isActive && session->userID == user_id) { session->isActive = false; session->is_active = false; }
    }
    unlock_sessions();
}
