#ifndef SOCKET_IO_H
#define SOCKET_IO_H
#include <sys/types.h>
ssize_t send_all(int fd, const void *buffer, size_t length);
ssize_t recv_all(int fd, void *buffer, size_t length);
#endif
