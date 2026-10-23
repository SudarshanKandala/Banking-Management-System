#ifndef SOCKET_IO_H
#define SOCKET_IO_H

#include <stddef.h>
#include <sys/types.h>

ssize_t send_all(int sock_fd, const void *buffer, size_t size);
ssize_t recv_all(int sock_fd, void *buffer, size_t size);

#endif