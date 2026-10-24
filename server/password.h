#ifndef PASSWORD_H
#define PASSWORD_H
#include <stddef.h>
int password_hash(const char *password, char *output, size_t output_size);
int password_verify(const char *password, const char *stored_hash);
#endif
