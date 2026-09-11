#pragma once
#include <cstddef>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <string_view>
#include <string>
#include "Status.hpp"

namespace nbs
{
class Socket{
private:
    int fd_ = -1;

public:
    Socket() = default;

    explicit Socket(int fd) : fd_(fd) {
        if (fd_ != -1 && !make_nonblocking()) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~Socket() {
        close();
    }

    bool make_nonblocking() {
        if (fd_ == -1) return false;

        int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags == -1) return false;

        return ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) != -1;
    }

    void close() {
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool is_valid() const noexcept {
        return fd_ != -1;
    }

    bool bind(uint16_t port) {
        if (fd_ == -1) return false;

        int opt = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
            return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = ::htons(port);

        return ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    bool listen(int backlog = 128) {
        if (fd_ == -1) return false;
        return ::listen(fd_, backlog) == 0;
    }

    int try_accept() {
        if (fd_ == -1) return IO_ACCEPT_ERROR;

        int client_fd = ::accept(fd_, nullptr, nullptr);
        if (client_fd >= 0) {
            int flags = ::fcntl(client_fd, F_GETFL, 0);
            if (flags == -1 || ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                ::close(client_fd);
                return IO_ACCEPT_ERROR;
            }
            return client_fd;
        }

        const int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)
            return IO_TRY_LATER;
        if (saved_errno == EINTR)
            return IO_INTERRUPTED;
        return IO_ACCEPT_ERROR;
    }

    ssize_t try_connect(std::string_view ip, uint16_t port) {
        if (fd_ == -1) return IO_ERROR;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);

        // inet_pton() expects a NUL-terminated string.
        std::string ip_string(ip);
        if (::inet_pton(AF_INET, ip_string.c_str(), &addr.sin_addr) <= 0)
            return IO_ERROR;

        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
            return 0;

        const int saved_errno = errno;
        if (saved_errno == EINPROGRESS || saved_errno == EALREADY)
            return IO_TRY_LATER;
        if (saved_errno == EINTR)
            return IO_INTERRUPTED;
        if (saved_errno == EISCONN)
            return 0;
        return IO_ERROR;
    }

    // Must be called after the non-blocking connect socket becomes writable.
    // SO_ERROR == 0 means the connection completed successfully.
    ssize_t finish_connect() const noexcept {
        if (fd_ == -1) return IO_ERROR;

        int error = 0;
        socklen_t len = sizeof(error);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) == -1)
            return IO_ERROR;

        if (error == 0)
            return 0;
        if (error == EINPROGRESS || error == EALREADY)
            return IO_TRY_LATER;
        return IO_ERROR;
    }

    ssize_t try_read(std::byte* buffer, std::size_t max_size) {
        if (fd_ == -1) return IO_ERROR;

        ssize_t result = ::recv(fd_, buffer, max_size, 0);
        if (result >= 0) return result; // 0 is EOF, not an error.

        const int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)
            return IO_TRY_LATER;
        if (saved_errno == EINTR)
            return IO_INTERRUPTED;
        return IO_ERROR;
    }

    ssize_t try_write(const std::byte* buffer, std::size_t size) {
        if (fd_ == -1) return IO_ERROR;

        ssize_t result = ::send(fd_, buffer, size, MSG_NOSIGNAL);
        if (result > 0) return result;
        if (result == 0) return 0;

        const int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)
            return IO_TRY_LATER;
        if (saved_errno == EINTR)
            return IO_INTERRUPTED;
        return IO_ERROR;
    }

    int get_fd() const noexcept { return fd_; }
};
} // namespace nbs
