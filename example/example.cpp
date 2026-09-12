#include <stdexcept>
#include <string>
#include <vector>
#include <nbsocket/Acceptor.hpp>
#include <nbsocket/SocketSession.hpp>
#include <boost/fiber/all.hpp>
#include <iostream>

constexpr int worker_count = 10;
constexpr const char* ip = "127.0.0.1";
constexpr uint16_t port = 8081;

const std::string from_server_str = "Hello client!";
const std::string from_client_str = "Sup server!";

std::atomic<bool> acceptor_running = true;
std::atomic<bool> acceptor_ready = false;

// Client side
void client()
{
    nbs::SocketSession session;

    while (true) {
        nbs::SessionResult res = session.try_connect(ip, port);

        if (res == nbs::SessionResult::Success) {
            break;
        }
        else if (res == nbs::SessionResult::InProgress) {
            boost::this_fiber::yield();
            continue;
        }
        else {
            throw std::runtime_error("No connection");
        }
    }

    const std::byte* write_ptr =
        reinterpret_cast<const std::byte*>(from_client_str.data());

    while (true) {
        nbs::SessionResult res =
            session.try_write_exact(write_ptr, from_client_str.size());

        if (res == nbs::SessionResult::Success) {
            break;
        }
        else if (res == nbs::SessionResult::InProgress) {
            boost::this_fiber::yield();
            continue;
        }
        else {
            throw std::runtime_error("Something went wrong");
        }
    }

    std::string request(from_server_str.size(), '\0');

    std::byte* read_ptr =
        reinterpret_cast<std::byte*>(request.data());

    while (true) {
        nbs::SessionResult res =
            session.try_read_exact(read_ptr, request.size());

        if (res == nbs::SessionResult::Success) {
            break;
        }
        else if (res == nbs::SessionResult::InProgress) {
            boost::this_fiber::yield();
            continue;
        }
        else {
            throw std::runtime_error("Something went wrong");
        }
    }

    if (request != from_server_str) {
        throw std::runtime_error("Wrong reply");
    }else{
        printf("Client got: %s\n", request.c_str());
    }
}

// Server side
void worker(nbs::SocketSession&& session)
{
    std::string request(from_client_str.size(), '\0');

    std::byte* read_ptr =
        reinterpret_cast<std::byte*>(request.data());

    // Получаем сообщение от клиента
    while (true) {
        nbs::SessionResult res =
            session.try_read_exact(read_ptr, request.size());

        if (res == nbs::SessionResult::Success) {
            break;
        }
        else if (res == nbs::SessionResult::InProgress) {
            boost::this_fiber::yield();
            continue;
        }
        else {
            throw std::runtime_error("Failed to read from client");
        }
    }

    if (request != from_client_str) {
        throw std::runtime_error("Wrong message from client");
    }
    else{
        printf("Server got: %s\n", request.c_str());
    }

    const std::byte* write_ptr =
        reinterpret_cast<const std::byte*>(from_server_str.data());

    while (true) {
        nbs::SessionResult res =
            session.try_write_exact(write_ptr, from_server_str.size());

        if (res == nbs::SessionResult::Success) {
            break;
        }
        else if (res == nbs::SessionResult::InProgress) {
            boost::this_fiber::yield();
            continue;
        }
        else {
            throw std::runtime_error("Failed to write to client");
        }
    }
}

void acceptor()
{
    nbs::Acceptor acceptor(port);

    if (!acceptor.is_active()) {
        throw std::runtime_error("Failed to create acceptor");
    }

    acceptor_ready.store(true, std::memory_order_release);

    while (acceptor_running.load(std::memory_order_acquire)) {
        nbs::SessionResult result;

        auto session = acceptor.try_accept(result);

        if (result == nbs::SessionResult::Success) {

            boost::fibers::fiber(
                [session = std::move(session)]() mutable {
                    try {
                        worker(std::move(session));
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Worker error: "
                                  << e.what() << '\n';
                    }
                }
            ).detach();

            continue;
        }

        if (result == nbs::SessionResult::InProgress) {
            boost::this_fiber::yield();
            continue;
        }

        throw std::runtime_error("Accept failed");
    }
}

int main(){
    printf("Starting server\n");

    boost::fibers::fiber server([] {
        acceptor();
    });

    while (!acceptor_ready.load(std::memory_order_acquire)) {
        boost::this_fiber::yield();
    }
    printf("Server started\n");

    std::vector<boost::fibers::fiber> clients;
    clients.reserve(worker_count);
    
    printf("Starting clients\n");
    for (int i = 0; i < worker_count; ++i) {
        clients.emplace_back([] {
            client();
        });
    }

    for (auto& client_fiber : clients) {
        client_fiber.join();
    }
    printf("All clients finished job\n");

    acceptor_running.store(false, std::memory_order_release);

    printf("Finishing the serevr\n");
    server.join();

    return 0;
}
