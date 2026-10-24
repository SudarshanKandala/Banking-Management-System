CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror
ROOT := ..
SOURCES := main.c connect.c login.c client_common.c customer_services.c \
           bank_employee_services.c bank_manager_services.c administrator_services.c

.PHONY: all clean
all: $(ROOT)/banking_client

$(ROOT)/banking_client: $(SOURCES) $(ROOT)/socket_comm/socket_io.c
	$(CC) $(CFLAGS) -I$(ROOT)/include -I$(ROOT)/socket_comm -o $@ $(SOURCES) $(ROOT)/socket_comm/socket_io.c

clean:
	rm -f $(ROOT)/banking_client
