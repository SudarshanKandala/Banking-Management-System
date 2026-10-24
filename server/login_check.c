#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>
#include "login_check.h"

/* glibc does not always expose union semun from <sys/sem.h>. */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

SharedSessionTable *session_table = NULL;
int session_shmid = -1;
static int session_semid = -1;
static _Thread_local int current_session_id = -1;

int initialize_session_store(void)
{
    union semun sem_arg;
    key_t SESSION_SHM_KEY = ftok("./", 'A');
    key_t SESSION_SEM_KEY = ftok("./", 'B');

    session_shmid = shmget(SESSION_SHM_KEY, sizeof(SharedSessionTable), IPC_CREAT | IPC_EXCL | 0600);
    if (session_shmid == -1) {
        perror("shmget session table failed; check for a stale or already-running server segment");
        return -1;
    }

    session_table = (SharedSessionTable *)shmat(session_shmid, NULL, 0);
    if (session_table == (void *)-1) {
        session_table = NULL;
        perror("shmat session table failed");
        shmctl(session_shmid, IPC_RMID, NULL);
        session_shmid = -1;
        return -1;
    }

    memset(session_table, 0, sizeof(*session_table));
    session_table->next_session_id = 1;

    session_semid = semget(SESSION_SEM_KEY, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (session_semid == -1) {
        perror("semget session semaphore failed; check for a stale or already-running server semaphore");
        shmdt(session_table);
        session_table = NULL;
        shmctl(session_shmid, IPC_RMID, NULL);
        session_shmid = -1;
        return -1;
    }

    sem_arg.val = 1;
    if (semctl(session_semid, 0, SETVAL, sem_arg) == -1) {
        perror("semctl SETVAL session semaphore failed");
        semctl(session_semid, 0, IPC_RMID);
        session_semid = -1;
        shmdt(session_table);
        session_table = NULL;
        shmctl(session_shmid, IPC_RMID, NULL);
        session_shmid = -1;
        return -1;
    }

    return 0;
}

void destroy_session_store(void)
{
    if (session_semid != -1) {
        if (semctl(session_semid, 0, IPC_RMID) == -1) perror("semctl IPC_RMID session semaphore failed");
        session_semid = -1;
    }

    if (session_table != NULL) {
        if (shmdt(session_table) == -1) perror("shmdt session table failed");
        session_table = NULL;
    }

    if (session_shmid != -1) {
        if (shmctl(session_shmid, IPC_RMID, NULL) == -1) perror("shmctl IPC_RMID session table failed");
        session_shmid = -1;
    }
}

static int lock_session_table(void)
{
    if (session_table == NULL || session_semid == -1) return -1;

    struct sembuf operation = {0, -1, 0};
    while (semop(session_semid, &operation, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop lock session table failed");
        return -1;
    }

    return 0;
}

static void unlock_session_table(void)
{
    if (session_table == NULL || session_semid == -1) return;

    struct sembuf operation = {0, 1, 0};
    while (semop(session_semid, &operation, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop unlock session table failed");
        return;
    }
}

static bool session_expired_locked(Session *session, time_t now)
{
    if (session->isActive == false) return true;
    if (session->expiryDuration <= 0) return true;
    return difftime(now, session->last_access_time) >= session->expiryDuration;
}

static void expire_old_sessions_locked(time_t now)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_table->sessions[i].isActive && session_expired_locked(&session_table->sessions[i], now)) session_table->sessions[i].isActive = false;
    }
}

static bool session_id_in_use_locked(int session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_table->sessions[i].isActive && session_table->sessions[i].sessionID == session_id) return true;
    }
    return false;
}

static int create_session(int user_id)
{
    if (lock_session_table() == -1) return 1;

    time_t now = time(NULL);
    expire_old_sessions_locked(now);

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_table->sessions[i].isActive && session_table->sessions[i].userID == user_id) {
            unlock_session_table();
            return 2;
        }
    }

    int free_slot = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!session_table->sessions[i].isActive) { free_slot = i; break; }
    }

    if (free_slot == -1) {
        unlock_session_table();
        fprintf(stderr, "Session table is full\n");
        return 1;
    }

    int new_session_id = session_table->next_session_id;
    if (new_session_id <= 0) new_session_id = 1;
    while (session_id_in_use_locked(new_session_id)) { new_session_id++; if (new_session_id <= 0) new_session_id = 1; }

    Session new_session = {0};
    new_session.sessionID = new_session_id;
    new_session.userID = user_id;
    new_session.login_time = now;
    new_session.last_access_time = now;
    new_session.isActive = true;
    new_session.expiryDuration = SESSION_TIMEOUT_SECONDS;
    session_table->sessions[free_slot] = new_session;

    session_table->next_session_id = new_session_id + 1;
    if (session_table->next_session_id <= 0) session_table->next_session_id = 1;
    current_session_id = new_session_id;
    unlock_session_table();
    return 0;
}

bool is_session_active(void)
{
    if (current_session_id < 0 || lock_session_table() == -1) return false;

    bool active = false;
    time_t now = time(NULL);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        Session *session = &session_table->sessions[i];
        if (session->isActive && session->sessionID == current_session_id) {
            if (session_expired_locked(session, now)) session->isActive = false;
            else { session->last_access_time = now; active = true; }
            break;
        }
    }

    unlock_session_table();
    if (!active) current_session_id = -1;
    return active;
}

int get_current_session_id(void)
{
    return current_session_id;
}

int get_current_user_id(void)
{
    if (!is_session_active() || lock_session_table() == -1) return -1;

    int user_id = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_table->sessions[i].isActive && session_table->sessions[i].sessionID == current_session_id) {
            user_id = session_table->sessions[i].userID;
            break;
        }
    }

    unlock_session_table();
    return user_id;
}

void logout_current_session(void)
{
    if (current_session_id < 0) return;

    if (lock_session_table() == 0) {
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (session_table->sessions[i].isActive && session_table->sessions[i].sessionID == current_session_id) {
                session_table->sessions[i].isActive = false;
                break;
            }
        }
        unlock_session_table();
    }

    current_session_id = -1;
}

int login_check(RequestHeader header, LoginRequest login_details){
         
}