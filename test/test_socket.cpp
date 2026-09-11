#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <array>
#include <nbsocket/Socket.hpp>

namespace nbs {
namespace {

TEST(SocketTest, DefaultConstructorInitializesToInvalid) {
    Socket sock;
    EXPECT_FALSE(sock.is_valid());
    EXPECT_EQ(sock.get_fd(), -1);
}

TEST(SocketTest, ConstructorWithValidFdMakesItNonBlocking) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    
    {
        Socket sock(sv[0]);
        EXPECT_TRUE(sock.is_valid());
        EXPECT_EQ(sock.get_fd(), sv[0]);

        int flags = ::fcntl(sv[0], F_GETFL, 0);
        EXPECT_TRUE(flags & O_NONBLOCK);
    }
    EXPECT_EQ(::close(sv[0]), -1);
    EXPECT_EQ(errno, EBADF);

    ::close(sv[1]);
}

TEST(SocketTest, MoveConstructorTransfersOwnership) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    {
        Socket origin(sv[0]);
        Socket moved(std::move(origin));

        EXPECT_FALSE(origin.is_valid());
        EXPECT_EQ(origin.get_fd(), -1);
        
        EXPECT_TRUE(moved.is_valid());
        EXPECT_EQ(moved.get_fd(), sv[0]);
    }
    EXPECT_EQ(::close(sv[0]), -1);
    EXPECT_EQ(errno, EBADF);
    
    ::close(sv[1]);
}

TEST(SocketTest, MoveAssignmentClosesExistingAndTakesNew) {
    int sv1[2];
    int sv2[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv1), 0);
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv2), 0);

    {
        Socket sock1(sv1[0]);
        Socket sock2(sv2[0]);

        sock1 = std::move(sock2);

        EXPECT_TRUE(sock1.is_valid());
        EXPECT_EQ(sock1.get_fd(), sv2[0]);
        EXPECT_FALSE(sock2.is_valid());

        EXPECT_EQ(::close(sv1[0]), -1);
        EXPECT_EQ(errno, EBADF);
    }
    EXPECT_EQ(::close(sv2[0]), -1);
    EXPECT_EQ(errno, EBADF);

    ::close(sv1[1]);
    ::close(sv2[1]);
}

TEST(SocketTest, ExplicitCloseInvalidatesSocket) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    Socket sock(sv[0]);
    EXPECT_TRUE(sock.is_valid());
    
    sock.close();
    EXPECT_FALSE(sock.is_valid());
    EXPECT_EQ(sock.get_fd(), -1);
    
    EXPECT_NO_THROW(sock.close());

    ::close(sv[1]);
}

TEST(SocketTest, TryReadAndTryWriteSuccess) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    Socket sender(sv[0]);
    Socket receiver(sv[1]);

    std::array<std::byte, 5> write_buf = { 
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5} 
    };
    std::array<std::byte, 5> read_buf{};

    ssize_t written = sender.try_write(write_buf.data(), write_buf.size());
    EXPECT_EQ(written, static_cast<ssize_t>(write_buf.size()));

    ssize_t read = receiver.try_read(read_buf.data(), read_buf.size());
    EXPECT_EQ(read, static_cast<ssize_t>(read_buf.size()));

    EXPECT_EQ(read_buf, write_buf);
}

TEST(SocketTest, TryReadReturnsTryLaterWhenNoData) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    Socket receiver(sv[1]);
    std::array<std::byte, 10> read_buf{};

    ssize_t result = receiver.try_read(read_buf.data(), read_buf.size());
    EXPECT_EQ(result, IO_TRY_LATER);

    ::close(sv[0]);
}

TEST(SocketTest, TryAcceptReturnsErrorOnInvalidState) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    Socket sock(sv[0]);
    
    int client_fd = sock.try_accept();
    EXPECT_EQ(client_fd, IO_ACCEPT_ERROR);

    ::close(sv[1]);
}

TEST(SocketTest, BindAndListenSuccess) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);

    Socket server_sock(fd);
    
    EXPECT_TRUE(server_sock.bind(0));
    EXPECT_TRUE(server_sock.listen(10));

    EXPECT_EQ(server_sock.try_accept(), IO_TRY_LATER);
}

// ==========================================
// TESTS FOR CONNECTION
// ==========================================

TEST(SocketTest, TryConnectReturnsErrorOnInvalidSocket) {
    Socket sock; // fd_ = -1
    ssize_t result = sock.try_connect("127.0.0.1", 8080);
    EXPECT_EQ(result, IO_ERROR);
}

TEST(SocketTest, TryConnectReturnsErrorOnInvalidIpFormat) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    Socket sock(fd);

    ssize_t result = sock.try_connect("invalid_ip_address", 8080);
    EXPECT_EQ(result, IO_ERROR);
}

TEST(SocketTest, TryConnectReturnsTryLaterWhenConnectingNonBlocking) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    Socket sock(fd);

    ssize_t result = sock.try_connect("8.8.8.8", 9999);
    
    EXPECT_TRUE(result == IO_TRY_LATER || result == IO_ERROR);
}

TEST(SocketTransmissionTest, SendAndReceiveDataSuccess) {
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(server_fd, 0);
    Socket server(server_fd);
    
    ASSERT_TRUE(server.bind(0));
    ASSERT_TRUE(server.listen(1));

    struct sockaddr_in server_addr{};
    socklen_t addr_len = sizeof(server_addr);
    ASSERT_EQ(::getsockname(server.get_fd(), reinterpret_cast<struct sockaddr*>(&server_addr), &addr_len), 0);
    uint16_t allocated_port = ::ntohs(server_addr.sin_port);

    int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);
    Socket client(client_fd);

    ssize_t conn_res = client.try_connect("127.0.0.1", allocated_port);
    ASSERT_TRUE(conn_res == 0 || conn_res == IO_TRY_LATER);

    int accepted_fd = IO_TRY_LATER;
    for (int i = 0; i < 100; ++i) {
        accepted_fd = server.try_accept();
        if (accepted_fd >= 0) break;
        ::usleep(1000); // подождем 1 мс
    }
    ASSERT_GE(accepted_fd, 0);
    Socket connected_client(accepted_fd);

    std::array<std::byte, 6> send_msg = {
        std::byte{'H'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}, std::byte{'!'}
    };
    std::array<std::byte, 6> recv_msg{};

    ssize_t bytes_written = 0;
    for (int i = 0; i < 100; ++i) {
        bytes_written = client.try_write(send_msg.data(), send_msg.size());
        if (bytes_written != IO_TRY_LATER) break;
        ::usleep(1000);
    }
    EXPECT_EQ(bytes_written, static_cast<ssize_t>(send_msg.size()));

    ssize_t bytes_read = 0;
    for (int i = 0; i < 100; ++i) {
        bytes_read = connected_client.try_read(recv_msg.data(), recv_msg.size());
        if (bytes_read != IO_TRY_LATER) break;
        ::usleep(1000);
    }
    EXPECT_EQ(bytes_read, static_cast<ssize_t>(recv_msg.size()));

    EXPECT_EQ(send_msg, recv_msg);
}

TEST(SocketTransmissionTest, SendAndReceiveLargeDataChunk) {
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(server_fd, 0);
    Socket server(server_fd);
    ASSERT_TRUE(server.bind(0));
    ASSERT_TRUE(server.listen(1));

    struct sockaddr_in server_addr{};
    socklen_t addr_len = sizeof(server_addr);
    ASSERT_EQ(::getsockname(server.get_fd(), reinterpret_cast<struct sockaddr*>(&server_addr), &addr_len), 0);
    uint16_t allocated_port = ::ntohs(server_addr.sin_port);

    int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    Socket client(client_fd);
    client.try_connect("127.0.0.1", allocated_port);

    int accepted_fd = IO_TRY_LATER;
    for (int i = 0; i < 100; ++i) {
        accepted_fd = server.try_accept();
        if (accepted_fd >= 0) break;
        ::usleep(1000);
    }
    ASSERT_GE(accepted_fd, 0);
    Socket connected_client(accepted_fd);

    const size_t data_size = 100 * 1024;
    std::vector<std::byte> send_data(data_size);
    for (size_t i = 0; i < data_size; ++i) {
        send_data[i] = static_cast<std::byte>(i % 256);
    }
    std::vector<std::byte> recv_data(data_size);

    size_t total_sent = 0;
    while (total_sent < data_size) {
        ssize_t sent = client.try_write(send_data.data() + total_sent, data_size - total_sent);
        if (sent > 0) {
            total_sent += sent;
        } else if (sent == IO_TRY_LATER) {
            ::usleep(500);
        } else {
            FAIL() << "Error during sending large data chung";
        }
    }

    size_t total_received = 0;
    int read_attempts = 0;
    while (total_received < data_size && read_attempts < 1000) {
        ssize_t received = connected_client.try_read(recv_data.data() + total_received, data_size - total_received);
        if (received > 0) {
            total_received += received;
        } else if (received == IO_TRY_LATER) {
            ::usleep(500); 
            read_attempts++;
        } else {
            FAIL() << "Error during recieving large data chung";
        }
    }

    EXPECT_EQ(total_received, data_size);
    EXPECT_EQ(send_data, recv_data);
}

} // namespace
} // namespace nbs
