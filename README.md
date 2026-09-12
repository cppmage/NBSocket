# NBSocket

A small non-blocking TCP socket library for Linux based on the POSIX Socket API.

## Features

- TCP sockets
- Non-blocking I/O
- RAII-based socket ownership
- Non-blocking `connect()` and `accept()`
- Exact-size reads and writes
- Handling of `EAGAIN` / `EWOULDBLOCK`, `EINTR`, and `EINPROGRESS`
- Connection state tracking
- Move-only socket/session types

## Basic usage

The main type used for communication is `nbs::SocketSession`.

### Connecting to a server

```cpp
nbs::SocketSession session;

while (true) {
    nbs::SessionResult result =
        session.try_connect("127.0.0.1", 8081);

    if (result == nbs::SessionResult::Success) {
        break;
    }

    if (result == nbs::SessionResult::InProgress) {
        // Wait for socket readiness and retry.
        continue;
    }

    throw std::runtime_error("Connection failed");
}
```

`try_connect()` is non-blocking. A connection that cannot be completed immediately returns `SessionResult::InProgress`.

The caller is responsible for deciding how to wait before retrying. For example, an application can use `poll`, `epoll`, or a fiber scheduler.

## Reading data

`try_read_exact()` reads exactly the requested number of bytes.

```cpp
std::string buffer(13, '\0');

std::byte* read_ptr =
    reinterpret_cast<std::byte*>(buffer.data());

while (true) {
    nbs::SessionResult result =
        session.try_read_exact(read_ptr, buffer.size());

    if (result == nbs::SessionResult::Success) {
        break;
    }

    if (result == nbs::SessionResult::InProgress) {
        // Wait for the socket to become readable.
        continue;
    }

    throw std::runtime_error("Read failed");
}
```

The operation can require multiple calls. The session keeps track of the pending operation, so the same buffer and size should be supplied until the operation completes.

If the peer closes the connection, the session becomes disconnected.

## Writing data

`try_write_exact()` behaves similarly and guarantees that the requested number of bytes is written before returning `Success`.

```cpp
const std::string message = "Hello client!";

const std::byte* write_ptr =
    reinterpret_cast<const std::byte*>(message.data());

while (true) {
    nbs::SessionResult result =
        session.try_write_exact(write_ptr, message.size());

    if (result == nbs::SessionResult::Success) {
        break;
    }

    if (result == nbs::SessionResult::InProgress) {
        // Wait for the socket to become writable.
        continue;
    }

    throw std::runtime_error("Write failed");
}
```

The library handles partial `send()` operations internally.

## Server side

Use `nbs::Acceptor` to create a listening TCP socket:

```cpp
nbs::Acceptor acceptor(8081);

if (!acceptor.is_active()) {
    throw std::runtime_error("Failed to create acceptor");
}
```

Accept clients with `try_accept()`:

```cpp
nbs::SessionResult result;

nbs::SocketSession session =
    acceptor.try_accept(result);

if (result == nbs::SessionResult::Success) {
    // Handle the new session.
}
else if (result == nbs::SessionResult::InProgress) {
    // No client is ready yet.
}
else {
    throw std::runtime_error("Accept failed");
}
```

`try_accept()` is non-blocking. When no connection is waiting, it returns `SessionResult::InProgress`.

## Result states

Operations use `nbs::SessionResult`:

```cpp
enum class SessionResult {
    Success,
    InProgress,
    Busy,
    Disconnected,
    Error
};
```

| Result | Meaning |
|---|---|
| `Success` | The requested operation completed |
| `InProgress` | The operation cannot make progress right now |
| `Busy` | Another operation of the same type is already pending |
| `Disconnected` | The peer is disconnected or the session is not connected |
| `Error` | A fatal socket error occurred |

## Boost.Fiber integration

NbSocket itself does not depend on Boost.Fiber. This keeps the socket layer independent from the scheduling mechanism.

It can, however, be used from Boost.Fiber by yielding while an operation is in progress:

```cpp
while (true) {
    nbs::SessionResult result =
        session.try_read_exact(read_ptr, size);

    if (result == nbs::SessionResult::Success) {
        break;
    }

    if (result == nbs::SessionResult::InProgress) {
        boost::this_fiber::yield();
        continue;
    }

    throw std::runtime_error("Read failed");
}
```

For a real event-driven application, `yield()` should be combined with an I/O readiness mechanism such as `poll` or `epoll`. Simply yielding does not wait for a file descriptor to become ready.

A typical architecture is:

```text
event loop
    |
    +-- acceptor
    |
    +-- fiber -> SocketSession
    |
    +-- fiber -> SocketSession
    |
    +-- fiber -> SocketSession
```

The acceptor can accept new connections and create a worker fiber for each session.

## Socket ownership

`Socket` and `SocketSession` use RAII and are move-only.

A session can therefore be transferred to another owner:

```cpp
boost::fibers::fiber(
    [session = std::move(session)]() mutable {
        worker(std::move(session));
    }
).detach();
```

After moving a session, the source object should not be used as the original session.

## Building

The library uses CMake.

If the project contains the Boost.Fiber example, Boost.Fiber is optional. The example is only added when CMake finds the required Boost component.

```bash
cmake -S . -B build
cmake --build build
```

Without Boost.Fiber, the core library can still be configured and built; the fiber example is skipped.

## Requirements

- Linux
- C++17 or newer
- CMake
- POSIX sockets
- Boost.Fiber (only for the fiber example)

## Design

The library intentionally provides a low-level non-blocking API instead of hiding I/O waiting inside the socket classes.

This makes it possible to integrate the same socket layer with different execution models, such as:

- `poll`
- `epoll`
- a custom event loop
- Boost.Fiber
- other cooperative schedulers

The application controls when to retry an operation after `InProgress`.
