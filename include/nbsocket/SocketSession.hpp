#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <sys/socket.h>
#include <string_view>
#include "Socket.hpp"
#include "Status.hpp"

namespace nbs
{
class SocketSession{
private:
    Socket socket_;
    SessionStatus status_ = SessionStatus::Disconnected;
    bool connect_in_progress_ = false;

    std::byte* pending_read_buf_ = nullptr;
    std::size_t pending_read_offset_ = 0;
    std::size_t pending_read_awaited_ = 0;

    const std::byte* pending_write_buf_ = nullptr;
    std::size_t pending_write_offset_ = 0;
    std::size_t pending_write_awaited_ = 0;

public:
    explicit SocketSession(Socket&& socket, SessionStatus mode)
        : socket_(std::move(socket)), status_(mode) {}

    explicit SocketSession(int fd, SessionStatus mode)
        : socket_(fd), status_(mode) {}

    SocketSession() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            status_ = SessionStatus::Dead;
            return;
        }

        Socket socket(fd);
        if (!socket.is_valid()) {
            status_ = SessionStatus::Dead;
            return;
        }

        socket_ = std::move(socket);
        status_ = SessionStatus::Disconnected;
    }

    SessionStatus status() const noexcept { return status_; }
    bool is_connected() const noexcept { return status_ == SessionStatus::Basic; }

    SessionResult try_connect(std::string_view ip, uint16_t port) {
        if (status_ == SessionStatus::Dead)
            return SessionResult::Error;

        if (status_ == SessionStatus::Basic)
            return SessionResult::Success;

        if (connect_in_progress_) {
            const ssize_t res = socket_.finish_connect();
            if (res == 0) {
                connect_in_progress_ = false;
                status_ = SessionStatus::Basic;
                return SessionResult::Success;
            }
            if (res == IO_TRY_LATER || res == IO_INTERRUPTED)
                return SessionResult::InProgress;

            connect_in_progress_ = false;
            status_ = SessionStatus::Dead;
            return SessionResult::Error;
        }

        const ssize_t res = socket_.try_connect(ip, port);
        if (res == 0) {
            status_ = SessionStatus::Basic;
            return SessionResult::Success;
        }

        if (res == IO_TRY_LATER || res == IO_INTERRUPTED) {
            connect_in_progress_ = true;
            return SessionResult::InProgress;
        }

        status_ = SessionStatus::Dead;
        return SessionResult::Error;
    }

    SessionResult try_read_exact(std::byte* buffer, std::size_t exact_size) {
        if (status_ == SessionStatus::Dead)
            return SessionResult::Error;

        if (status_ == SessionStatus::Disconnected)
            return SessionResult::Disconnected;

        if (exact_size == 0)
            return SessionResult::Success;

        if (pending_read_buf_ != nullptr) {
            if (pending_read_buf_ != buffer || pending_read_awaited_ != exact_size)
                return SessionResult::Busy;
        } else {
            pending_read_buf_ = buffer;
            pending_read_awaited_ = exact_size;
            pending_read_offset_ = 0;
        }

        const std::size_t bytes_left = pending_read_awaited_ - pending_read_offset_;
        const ssize_t res = socket_.try_read(
            pending_read_buf_ + pending_read_offset_, bytes_left);

        if (res > 0) {
            pending_read_offset_ += static_cast<std::size_t>(res);
            if (pending_read_offset_ == pending_read_awaited_) {
                pending_read_buf_ = nullptr;
                pending_read_offset_ = 0;
                pending_read_awaited_ = 0;
                return SessionResult::Success;
            }
            return SessionResult::InProgress;
        }

        if (res == 0) {
            // recv() == 0 is TCP EOF: the peer closed its sending side.
            // It is deliberately different from a zero-byte request above.
            status_ = SessionStatus::Disconnected;
            pending_read_buf_ = nullptr;
            pending_read_offset_ = 0;
            pending_read_awaited_ = 0;
            return SessionResult::Disconnected;
        }

        if (res == IO_TRY_LATER || res == IO_INTERRUPTED)
            return SessionResult::InProgress;

        status_ = SessionStatus::Dead;
        pending_read_buf_ = nullptr;
        pending_read_offset_ = 0;
        pending_read_awaited_ = 0;
        return SessionResult::Error;
    }

    SessionResult try_write_exact(const std::byte* data, std::size_t size) {
        if (status_ == SessionStatus::Dead)
            return SessionResult::Error;

        if (status_ == SessionStatus::Disconnected)
            return SessionResult::Disconnected;

        if (size == 0)
            return SessionResult::Success;

        if (pending_write_buf_ != nullptr) {
            if (pending_write_buf_ != data || pending_write_awaited_ != size)
                return SessionResult::Busy;
        } else {
            pending_write_buf_ = data;
            pending_write_awaited_ = size;
            pending_write_offset_ = 0;
        }

        const std::size_t bytes_left = pending_write_awaited_ - pending_write_offset_;
        const ssize_t res = socket_.try_write(
            pending_write_buf_ + pending_write_offset_, bytes_left);

        if (res > 0) {
            pending_write_offset_ += static_cast<std::size_t>(res);
            if (pending_write_offset_ == pending_write_awaited_) {
                pending_write_buf_ = nullptr;
                pending_write_offset_ = 0;
                pending_write_awaited_ = 0;
                return SessionResult::Success;
            }
            return SessionResult::InProgress;
        }

        if (res == IO_TRY_LATER || res == IO_INTERRUPTED)
            return SessionResult::InProgress;

        // send() returning 0 for a non-zero length is not progress; treat it
        // as a connection failure rather than spinning forever.
        status_ = SessionStatus::Dead;
        pending_write_buf_ = nullptr;
        pending_write_offset_ = 0;
        pending_write_awaited_ = 0;
        return SessionResult::Error;
    }

    SocketSession(const SocketSession&) = delete;
    SocketSession& operator=(const SocketSession&) = delete;

    SocketSession(SocketSession&& other) noexcept
        : socket_(std::move(other.socket_)),
          status_(other.status_),
          connect_in_progress_(other.connect_in_progress_),
          pending_read_buf_(other.pending_read_buf_),
          pending_read_offset_(other.pending_read_offset_),
          pending_read_awaited_(other.pending_read_awaited_),
          pending_write_buf_(other.pending_write_buf_),
          pending_write_offset_(other.pending_write_offset_),
          pending_write_awaited_(other.pending_write_awaited_) {
        other.status_ = SessionStatus::Dead;
        other.connect_in_progress_ = false;
        other.pending_read_buf_ = nullptr;
        other.pending_read_offset_ = 0;
        other.pending_read_awaited_ = 0;
        other.pending_write_buf_ = nullptr;
        other.pending_write_offset_ = 0;
        other.pending_write_awaited_ = 0;
    }

    SocketSession& operator=(SocketSession&& other) noexcept {
        if (this != &other) {
            socket_ = std::move(other.socket_);
            status_ = other.status_;
            connect_in_progress_ = other.connect_in_progress_;
            pending_read_buf_ = other.pending_read_buf_;
            pending_read_offset_ = other.pending_read_offset_;
            pending_read_awaited_ = other.pending_read_awaited_;
            pending_write_buf_ = other.pending_write_buf_;
            pending_write_offset_ = other.pending_write_offset_;
            pending_write_awaited_ = other.pending_write_awaited_;

            other.status_ = SessionStatus::Dead;
            other.connect_in_progress_ = false;
            other.pending_read_buf_ = nullptr;
            other.pending_read_offset_ = 0;
            other.pending_read_awaited_ = 0;
            other.pending_write_buf_ = nullptr;
            other.pending_write_offset_ = 0;
            other.pending_write_awaited_ = 0;
        }
        return *this;
    }

    ~SocketSession() = default;
};
} // namespace nbs
