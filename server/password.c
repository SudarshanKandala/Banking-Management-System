#define _GNU_SOURCE
#include "password.h"

#include <crypt.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>

/* Use SHA-512 crypt with a per-password random salt instead of storing plain text. */
int password_hash(const char *password, char *output, size_t output_size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./";
    unsigned char random_bytes[16];
    size_t have = 0;
    while (have < sizeof(random_bytes)) {
        ssize_t n = getrandom(random_bytes + have, sizeof(random_bytes) - have, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { perror("getrandom password salt"); return -1; }
        have += (size_t)n;
    }

    char salt[64] = "$6$rounds=10000$";
    size_t pos = strlen(salt);
    for (size_t i = 0; i < sizeof(random_bytes); i++) salt[pos++] = alphabet[random_bytes[i] & 63U];
    salt[pos] = '$';
    salt[pos + 1] = '\0';

    struct crypt_data data = {0};
    char *hashed = crypt_r(password, salt, &data);
    if (!hashed || strlen(hashed) >= output_size) { fprintf(stderr, "Unable to hash password\n"); return -1; }
    snprintf(output, output_size, "%s", hashed);
    return 0;
}

/* crypt_r is thread-safe and recomputes the stored scheme/salt for comparison. */
int password_verify(const char *password, const char *stored_hash)
{
    if (!password || !stored_hash || stored_hash[0] == '\0') return 0;
    struct crypt_data data = {0};
    char *candidate = crypt_r(password, stored_hash, &data);
    if (!candidate) return 0;
    return strcmp(candidate, stored_hash) == 0;
}
