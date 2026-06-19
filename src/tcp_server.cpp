#include "rotator/tcp_server.hpp"

#include "rotator/easycomm.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

namespace rotator {
namespace {

bool send_all(int socket, const std::string& message) {
    std::size_t sent = 0;
    while (sent < message.size()) {
        const auto count = ::send(socket, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

std::string execute(std::string_view line, RotatorController& controller) {
    const Command command = parse_easycomm(line);
    switch (command.kind) {
        case CommandKind::query_position: {
            const Position position = controller.position();
            return format_position(position.azimuth, position.elevation);
        }
        case CommandKind::set_position: {
            std::string error;
            if (!controller.set_target(command.azimuth, command.elevation, error)) {
                return "ERR " + error + "\r\n";
            }
            return {};
        }
        case CommandKind::stop:
            controller.stop();
            return {};
        case CommandKind::zero:
            controller.zero_current_position();
            return "OK ZERO\r\n";
        case CommandKind::park: {
            std::string error;
            if (!controller.set_target(0.0, 0.0, error)) {
                return "ERR " + error + "\r\n";
            }
            return "OK PARK\r\n";
        }
        case CommandKind::invalid:
            return "ERR " + command.error + "\r\n";
    }
    return "ERR invalid command\r\n";
}

void serve_client(int client, RotatorController& controller) {
    std::string pending;
    char buffer[512];
    while (true) {
        const auto count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            return;
        }
        pending.append(buffer, static_cast<std::size_t>(count));
        if (pending.size() > 4096) {
            send_all(client, "ERR command too long\r\n");
            return;
        }
        std::size_t newline = 0;
        while ((newline = pending.find('\n')) != std::string::npos) {
            const std::string response = execute(pending.substr(0, newline), controller);
            pending.erase(0, newline + 1);
            if (!response.empty() && !send_all(client, response)) {
                return;
            }
        }
    }
}

}  // namespace

TcpServer::TcpServer(std::uint16_t port, RotatorController& controller)
    : port_(port), controller_(controller) {}

int TcpServer::run(std::atomic_bool& stopping) {
    const int server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "socket: " << std::strerror(errno) << '\n';
        return 1;
    }
    const int reuse = 1;
    ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(server, 4) < 0) {
        std::cerr << "listen: " << std::strerror(errno) << '\n';
        ::close(server);
        return 1;
    }

    std::cout << "EasyComm simulator listening on TCP port " << port_ << '\n';
    while (!stopping.load()) {
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(server, &sockets);
        timeval timeout{.tv_sec = 1, .tv_usec = 0};
        const int ready = ::select(server + 1, &sockets, nullptr, nullptr, &timeout);
        if (ready < 0 && errno != EINTR) {
            std::cerr << "select: " << std::strerror(errno) << '\n';
            break;
        }
        if (ready > 0) {
            const int client = ::accept(server, nullptr, nullptr);
            if (client >= 0) {
                serve_client(client, controller_);
                ::close(client);
            }
        }
    }
    ::close(server);
    return 0;
}

}  // namespace rotator
