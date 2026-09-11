#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include "Socket.hpp"
#include "Status.hpp"
#include "SocketSession.hpp"

namespace nbs
{
class Acceptor{
private:
    Socket socket_;
    AcceptorStatus status_ = AcceptorStatus::Inactive;
    uint16_t port_ = 0;

public:
    Acceptor(uint16_t port, int fd) : port_(port) {
        init(fd);
    }

    explicit Acceptor(uint16_t port) : port_(port) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;
        init(fd);
    }

    SessionResult init(int fd) {
        if (fd == -1 || status_ != AcceptorStatus::Inactive)
            return SessionResult::Error;

        Socket socket(fd);
        if (!socket.is_valid())
            return SessionResult::Error;

        if (!socket.bind(port_))
            return SessionResult::Error;

        if (!socket.listen())
            return SessionResult::Error;

        socket_ = std::move(socket);
        status_ = AcceptorStatus::Active;
        return SessionResult::Success;
    }

    AcceptorStatus status() const noexcept { return status_; }
    bool is_active() const noexcept { return status_ == AcceptorStatus::Active; }
    uint16_t get_port() const noexcept { return port_; }

    // Returns an empty/dead session for "no connection available" or fatal
    // errors. Use try_accept(SessionResult&) when the caller needs to
    // distinguish InProgress from Error.
    SocketSession try_accept() {
        SessionResult result = SessionResult::Error;
        return try_accept(result);
    }

    // Non-breaking detailed API: the returned session is valid only when
    // result == Success. InProgress means EAGAIN/EINTR; Error is fatal.
    SocketSession try_accept(SessionResult& result) {
        if (!socket_.is_valid() || status_ != AcceptorStatus::Active) {
            result = SessionResult::Error;
            return SocketSession(-1, SessionStatus::Dead);
        }

        const int client_fd = socket_.try_accept();
        if (client_fd == IO_TRY_LATER || client_fd == IO_INTERRUPTED) {
            result = SessionResult::InProgress;
            return SocketSession(-1, SessionStatus::Dead);
        }

        if (client_fd < 0) {
            result = SessionResult::Error;
            return SocketSession(-1, SessionStatus::Dead);
        }

        result = SessionResult::Success;
        return SocketSession(client_fd, SessionStatus::Basic);
    }
};
} // namespace nbs
