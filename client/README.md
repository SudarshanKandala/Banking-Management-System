# Banking Management System — Client

This directory is a replacement for the project's existing `client/` directory. It does not contain or replace `include/` or `socket_comm/`.

## Build

From the project root, run:

```sh
make -C client
```

This creates `./banking_client` at the project root. Run it with:

```sh
./banking_client
```

The client uses the project's existing `include/config.h`, `include/enums.h`, `include/types.h`, `include/comm.h`, and `socket_comm/socket_io.c`.

## Wire protocol

- Login sends `RequestHeader` followed by `LoginRequest`, then receives `LoginResponse` (`0` success, `1` failed, `2` already logged in elsewhere).
- Every authenticated request sends `RequestHeader` followed by the full, zero-initialized `ServiceRequest` union.
- Each service response is a `ServiceResponse`. If it identifies a list, the client receives and prints the specified number of records (`PassbookEntry`, `LoanApplication`, `Feedback`, `SystemLog`, or `Configuration`).

Use the matching server headers and build the client/server on the same ABI, as the protocol sends native C structures directly over the local TCP connection.
