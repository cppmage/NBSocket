#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <chrono>

#include <nbsocket/Acceptor.hpp>
#include <nbsocket/SocketSession.hpp>

namespace
{

using namespace nbs;

constexpr int kMaxRetries = 100;
constexpr useconds_t kRetryDelayUs = 1000;

uint16_t find_free_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_NE(fd, -1);

    if (fd == -1) {
        return 0;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    const int bind_result =
        ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    EXPECT_EQ(bind_result, 0);

    if (bind_result != 0) {
        ::close(fd);
        return 0;
    }

    socklen_t addr_len = sizeof(addr);

    const int getsockname_result =
        ::getsockname(
            fd,
            reinterpret_cast<sockaddr*>(&addr),
            &addr_len);

    EXPECT_EQ(getsockname_result, 0);

    if (getsockname_result != 0) {
        ::close(fd);
        return 0;
    }

    const uint16_t port = ntohs(addr.sin_port);

    ::close(fd);

    return port;
}

bool connect_localhost(
    SocketSession& client,
    uint16_t port)
{
    for (int i = 0; i < kMaxRetries; ++i) {
        const SessionResult result =
            client.try_connect("127.0.0.1", port);

        if (result == SessionResult::Success) {
            return true;
        }

        if (result == SessionResult::Error ||
            result == SessionResult::Disconnected) {
            return false;
        }

        ::usleep(kRetryDelayUs);
    }

    return false;
}

bool accept_client(
    Acceptor& acceptor,
    SocketSession& server_session)
{
    for (int i = 0; i < kMaxRetries; ++i) {
        SessionResult result = SessionResult::Error;

        SocketSession session = acceptor.try_accept(result);

        if (result == SessionResult::Success) {
            server_session = std::move(session);
            return true;
        }

        if (result == SessionResult::Error) {
            return false;
        }

        ::usleep(kRetryDelayUs);
    }

    return false;
}

bool write_all(
    SocketSession& session,
    const std::vector<std::byte>& data)
{
    for (int i = 0; i < kMaxRetries; ++i) {
        const SessionResult result =
            session.try_write_exact(data.data(), data.size());

        if (result == SessionResult::Success) {
            return true;
        }

        if (result == SessionResult::Error ||
            result == SessionResult::Disconnected) {
            return false;
        }

        ::usleep(kRetryDelayUs);
    }

    return false;
}

bool read_all(
    SocketSession& session,
    std::vector<std::byte>& data)
{
    for (int i = 0; i < kMaxRetries; ++i) {
        const SessionResult result =
            session.try_read_exact(data.data(), data.size());

        if (result == SessionResult::Success) {
            return true;
        }

        if (result == SessionResult::Error ||
            result == SessionResult::Disconnected) {
            return false;
        }

        ::usleep(kRetryDelayUs);
    }

    return false;
}

} // namespace

TEST(AcceptorTest, StartsActiveWithFreePort)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);

    EXPECT_TRUE(acceptor.is_active());
    EXPECT_EQ(acceptor.status(), AcceptorStatus::Active);
    EXPECT_EQ(acceptor.get_port(), port);
}

TEST(AcceptorTest, RejectsSecondInit)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);

    ASSERT_TRUE(acceptor.is_active());

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(fd, -1);

    EXPECT_EQ(
        acceptor.init(fd),
        SessionResult::Error);

    ::close(fd);

    EXPECT_TRUE(acceptor.is_active());
}

TEST(AcceptorTest, TryAcceptWithoutClientReturnsInProgress)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);

    ASSERT_TRUE(acceptor.is_active());

    SessionResult result = SessionResult::Error;

    SocketSession session = acceptor.try_accept(result);

    EXPECT_EQ(result, SessionResult::InProgress);
    EXPECT_EQ(session.status(), SessionStatus::Dead);
}

TEST(AcceptorTest, AcceptsLocalhostConnection)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;

    ASSERT_TRUE(connect_localhost(client, port));
    ASSERT_TRUE(client.is_connected());

    SocketSession server;

    ASSERT_TRUE(accept_client(acceptor, server));
    EXPECT_TRUE(server.is_connected());
    EXPECT_EQ(server.status(), SessionStatus::Basic);
}

TEST(AcceptorTest, AcceptedSessionCanReceiveData)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;
    ASSERT_TRUE(accept_client(acceptor, server));

    const std::string text = "hello from client";

    const std::vector<std::byte> sent(
        reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data() + text.size()));

    ASSERT_TRUE(write_all(client, sent));

    std::vector<std::byte> received(sent.size());

    ASSERT_TRUE(read_all(server, received));

    EXPECT_EQ(received, sent);
}

TEST(SocketSessionTest, DefaultConstructorStartsDisconnected)
{
    SocketSession session;

    EXPECT_EQ(
        session.status(),
        SessionStatus::Disconnected);

    EXPECT_FALSE(session.is_connected());
}

TEST(SocketSessionTest, ConnectsToLocalhost)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;

    ASSERT_TRUE(connect_localhost(client, port));

    EXPECT_EQ(
        client.status(),
        SessionStatus::Basic);

    EXPECT_TRUE(client.is_connected());
}

TEST(SocketSessionTest, ConnectCompletesWithAccept)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;

    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;

    ASSERT_TRUE(accept_client(acceptor, server));

    EXPECT_TRUE(client.is_connected());
    EXPECT_TRUE(server.is_connected());
}

TEST(SocketSessionTest, ConnectToInvalidIpReturnsError)
{
    SocketSession client;

    EXPECT_EQ(
        client.try_connect("not-an-ip", 12345),
        SessionResult::Error);

    EXPECT_EQ(
        client.status(),
        SessionStatus::Dead);
}

TEST(SocketSessionTest, ConnectToUnusedPortEventuallyFails)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    SocketSession client;

    bool failed = false;

    for (int i = 0; i < kMaxRetries; ++i) {
        const SessionResult result =
            client.try_connect("127.0.0.1", port);

        if (result == SessionResult::Error) {
            failed = true;
            break;
        }

        if (result == SessionResult::Success) {
            break;
        }

        ::usleep(kRetryDelayUs);
    }

    EXPECT_TRUE(failed);
    EXPECT_EQ(client.status(), SessionStatus::Dead);
}

TEST(SocketSessionTest, ZeroLengthReadReturnsSuccess)
{
    SocketSession session(
        -1,
        SessionStatus::Dead);

    std::byte buffer{};

    EXPECT_EQ(
        session.try_read_exact(&buffer, 0),
        SessionResult::Error);
}

TEST(SocketSessionTest, ZeroLengthReadOnConnectedSessionReturnsSuccess)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;
    ASSERT_TRUE(accept_client(acceptor, server));

    std::byte buffer{};

    EXPECT_EQ(
        server.try_read_exact(&buffer, 0),
        SessionResult::Success);
}

TEST(SocketSessionTest, ZeroLengthWriteReturnsSuccess)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;
    ASSERT_TRUE(accept_client(acceptor, server));

    const std::byte* data = nullptr;

    EXPECT_EQ(
        client.try_write_exact(data, 0),
        SessionResult::Success);
}

TEST(SocketSessionTest, WriteAndReadExactData)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;
    ASSERT_TRUE(accept_client(acceptor, server));

    const std::string text =
        "The quick brown fox jumps over the lazy dog";

    const std::vector<std::byte> sent(
        reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data() + text.size()));

    ASSERT_TRUE(write_all(client, sent));

    std::vector<std::byte> received(sent.size());

    ASSERT_TRUE(read_all(server, received));

    EXPECT_EQ(received, sent);
}

TEST(SocketSessionTest, ReadWithoutDataReturnsInProgress)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;
    ASSERT_TRUE(accept_client(acceptor, server));

    std::array<std::byte, 16> buffer{};

    EXPECT_EQ(
        server.try_read_exact(buffer.data(), buffer.size()),
        SessionResult::InProgress);
}

TEST(SocketSessionTest, PartialReadRemainsInProgress)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;
    ASSERT_TRUE(accept_client(acceptor, server));

    const std::string first = "hello";
    const std::string second = " world";

    const std::vector<std::byte> first_data(
        reinterpret_cast<const std::byte*>(first.data()),
        reinterpret_cast<const std::byte*>(first.data() + first.size()));

    const std::vector<std::byte> second_data(
        reinterpret_cast<const std::byte*>(second.data()),
        reinterpret_cast<const std::byte*>(second.data() + second.size()));

    std::array<std::byte, 11> received{};

    ASSERT_TRUE(write_all(client, first_data));

    EXPECT_EQ(
        server.try_read_exact(received.data(), received.size()),
        SessionResult::InProgress);

    ASSERT_TRUE(write_all(client, second_data));

    SessionResult result = SessionResult::InProgress;

    for (int i = 0; i < kMaxRetries; ++i) {
        result =
            server.try_read_exact(received.data(), received.size());

        if (result == SessionResult::Success ||
            result == SessionResult::Error ||
            result == SessionResult::Disconnected) {
            break;
        }

        ::usleep(kRetryDelayUs);
    }

    ASSERT_EQ(result, SessionResult::Success);

    const std::string received_text(
        reinterpret_cast<const char*>(received.data()),
        received.size());

    EXPECT_EQ(received_text, "hello world");
}

TEST(SocketSessionTest, DifferentReadBufferWhileOperationIsPendingReturnsBusy)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;
    ASSERT_TRUE(accept_client(acceptor, server));

    std::array<std::byte, 8> first_buffer{};
    std::array<std::byte, 8> second_buffer{};

    EXPECT_EQ(
        server.try_read_exact(first_buffer.data(), first_buffer.size()),
        SessionResult::InProgress);

    EXPECT_EQ(
        server.try_read_exact(second_buffer.data(), second_buffer.size()),
        SessionResult::Busy);
}

TEST(SocketSessionTest, DifferentWriteBufferWhileOperationIsPendingReturnsBusy)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession server;
    ASSERT_TRUE(accept_client(acceptor, server));

    std::array<std::byte, 8> first_buffer{};
    std::array<std::byte, 8> second_buffer{};

    EXPECT_EQ(
        client.try_write_exact(first_buffer.data(), first_buffer.size()),
        SessionResult::Success);

    EXPECT_EQ(
        client.try_write_exact(second_buffer.data(), second_buffer.size()),
        SessionResult::Success);
}

TEST(SocketSessionTest, PeerCloseReturnsDisconnected)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession server;

    {
        SocketSession client;

        ASSERT_TRUE(connect_localhost(client, port));
        ASSERT_TRUE(accept_client(acceptor, server));

        ASSERT_TRUE(client.is_connected());
        ASSERT_TRUE(server.is_connected());
    }

    std::array<std::byte, 1> buffer{};

    SessionResult result = SessionResult::InProgress;

    for (int i = 0; i < kMaxRetries; ++i) {
        result =
            server.try_read_exact(buffer.data(), buffer.size());

        if (result == SessionResult::Disconnected ||
            result == SessionResult::Error) {
            break;
        }

        ::usleep(kRetryDelayUs);
    }

    EXPECT_EQ(result, SessionResult::Disconnected);
    EXPECT_EQ(
        server.status(),
        SessionStatus::Disconnected);
}

TEST(SocketSessionTest, DisconnectedSessionCannotRead)
{
    SocketSession session(
        -1,
        SessionStatus::Disconnected);

    std::byte buffer{};

    EXPECT_EQ(
        session.try_read_exact(&buffer, 1),
        SessionResult::Disconnected);
}

TEST(SocketSessionTest, DisconnectedSessionCannotWrite)
{
    SocketSession session(
        -1,
        SessionStatus::Disconnected);

    const std::byte data{};

    EXPECT_EQ(
        session.try_write_exact(&data, 1),
        SessionResult::Disconnected);
}

TEST(SocketSessionTest, DeadSessionReturnsError)
{
    SocketSession session(
        -1,
        SessionStatus::Dead);

    std::byte buffer{};

    EXPECT_EQ(
        session.try_read_exact(&buffer, 1),
        SessionResult::Error);

    EXPECT_EQ(
        session.try_write_exact(&buffer, 1),
        SessionResult::Error);

    EXPECT_EQ(
        session.try_connect("127.0.0.1", 12345),
        SessionResult::Error);
}

TEST(SocketSessionTest, MoveConstructionTransfersConnectedSession)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession moved(std::move(client));

    EXPECT_TRUE(moved.is_connected());
    EXPECT_EQ(moved.status(), SessionStatus::Basic);
    EXPECT_EQ(client.status(), SessionStatus::Dead);
}

TEST(SocketSessionTest, MoveAssignmentTransfersConnectedSession)
{
    const uint16_t port = find_free_port();
    ASSERT_NE(port, 0);

    Acceptor acceptor(port);
    ASSERT_TRUE(acceptor.is_active());

    SocketSession client;
    ASSERT_TRUE(connect_localhost(client, port));

    SocketSession target;

    target = std::move(client);

    EXPECT_TRUE(target.is_connected());
    EXPECT_EQ(target.status(), SessionStatus::Basic);
    EXPECT_EQ(client.status(), SessionStatus::Dead);
}
