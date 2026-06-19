#include "rotator/rotator.hpp"
#include "rotator/tcp_server.hpp"
#include "rotator/web_server.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string request(std::uint16_t port, std::string_view method, std::string_view target) {
    const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0) {
        throw std::runtime_error("test socket failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(socket);
        throw std::runtime_error("test connection failed");
    }
    const std::string message = std::string(method) + " " + std::string(target) +
                                " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    ::send(socket, message.data(), message.size(), MSG_NOSIGNAL);
    std::string response;
    char buffer[2048];
    while (true) {
        const auto count = ::recv(socket, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(count));
    }
    ::close(socket);
    return response;
}

int open_stalled_client(std::uint16_t port) {
    const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0) {
        throw std::runtime_error("stalled-client socket failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(socket);
        throw std::runtime_error("stalled-client connection failed");
    }
    return socket;
}
}

int main() {
    constexpr std::uint16_t easycomm_port = 24553;
    constexpr std::uint16_t web_port = 28080;
    std::atomic_bool stopping{false};
    rotator::RotatorController controller;
    rotator::TcpServer easycomm(easycomm_port, controller);
    rotator::WebServer web(web_port, "127.0.0.1", easycomm_port);

    std::thread easycomm_thread([&] { easycomm.run(stopping); });
    std::thread web_thread([&] { web.run(stopping); });
    std::this_thread::sleep_for(150ms);

    const int stalled_client = open_stalled_client(easycomm_port);

    const auto page = request(web_port, "GET", "/");
    require(page.find("200 OK") != std::string::npos, "serve control page");
    require(page.find("Satellite Rotator") != std::string::npos, "page title");

    const auto initial = request(web_port, "GET", "/api/status");
    require(initial.find("\"azimuth\":0.0") != std::string::npos, "initial azimuth");
    require(initial.find("\"elevation\":0.0") != std::string::npos, "initial elevation");
    require(initial.find("\"ok\":true") != std::string::npos,
            "web proxy works while another EasyComm client is idle");

    require(request(web_port, "POST", "/api/move?az=123.4&el=45.6").find("200 OK") !=
                std::string::npos,
            "move endpoint");
    const auto moved = request(web_port, "GET", "/api/status");
    require(moved.find("\"azimuth\":123.4") != std::string::npos, "moved azimuth");
    require(moved.find("\"elevation\":45.6") != std::string::npos, "moved elevation");

    require(request(web_port, "POST", "/api/zero").find("200 OK") != std::string::npos,
            "zero endpoint");
    const auto zeroed = request(web_port, "GET", "/api/status");
    require(zeroed.find("\"azimuth\":0.0") != std::string::npos, "zeroed azimuth");

    request(web_port, "POST", "/api/move?az=50&el=30");
    require(request(web_port, "POST", "/api/park").find("200 OK") != std::string::npos,
            "park endpoint");
    const auto parked = request(web_port, "GET", "/api/status");
    require(parked.find("\"azimuth\":0.0") != std::string::npos, "parked azimuth");
    require(parked.find("\"elevation\":0.0") != std::string::npos, "parked elevation");

    require(request(web_port, "POST", "/api/stop").find("200 OK") != std::string::npos,
            "stop endpoint");

    ::close(stalled_client);
    stopping.store(true);
    easycomm_thread.join();
    web_thread.join();
    std::cout << "All web integration tests passed\n";
    return 0;
}
