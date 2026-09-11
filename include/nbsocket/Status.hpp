#pragma once
#include <unistd.h>

namespace nbs
{
inline constexpr ssize_t IO_TRY_LATER    = -1; // EAGAIN / EWOULDBLOCK
inline constexpr ssize_t IO_INTERRUPTED  = -2; // EINTR
inline constexpr ssize_t IO_ERROR        = -3; // Fatal socket error
inline constexpr ssize_t IO_ACCEPT_ERROR = -4; // Fatal accept error

enum class SessionResult {
    Success,
    InProgress,
    Busy,
    Disconnected,
    Error
};

enum class SessionStatus{
    Disconnected, // No established connection (initial state / peer closed)
    Basic,        // Connected: read and write are available
    Dead          // Invalid/fatal state
};

enum class AcceptorStatus{
    Active,
    Inactive
};

} // namespace nbs
