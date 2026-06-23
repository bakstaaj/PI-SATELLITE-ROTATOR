#include "rotator/tcp_server.hpp"

#include "rotator/easycomm.hpp"

#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

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

std::string json_escape(std::string_view value) {
    std::string result;
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            result.push_back('\\');
            result.push_back(c);
        } else if (c == '\n' || c == '\r') {
            result.push_back(' ');
        } else {
            result.push_back(c);
        }
    }
    return result;
}

const char* json_bool(bool value) { return value ? "true" : "false"; }

std::string format_status_json(const ControllerStatus& status) {
    std::ostringstream body;
    body << std::fixed << std::setprecision(1)
         << "{\"ok\":true"
         << ",\"azimuth\":" << status.azimuth
         << ",\"elevation\":" << status.elevation
         << ",\"target_azimuth\":" << status.target_azimuth
         << ",\"target_elevation\":" << status.target_elevation
         << ",\"moving\":" << json_bool(status.moving)
         << ",\"external_feedback\":" << json_bool(status.external_feedback)
         << ",\"feedback_received\":" << json_bool(status.feedback_received)
         << ",\"feedback_stale\":" << json_bool(status.feedback_stale)
         << ",\"feedback_age_ms\":" << status.feedback_age_ms
         << ",\"fault\":" << json_bool(status.fault)
         << ",\"fault_reason\":\"" << json_escape(status.fault_reason) << "\""
         << ",\"motor_backend\":\"" << json_escape(status.motor_backend) << "\""
         << ",\"motor_fault\":" << json_bool(status.motor_fault)
         << ",\"motor_fault_reason\":\"" << json_escape(status.motor_fault_reason) << "\""
         << ",\"backend\":\"easycomm\"}\r\n";
    return body.str();
}

std::string execute(std::string_view line, RotatorController& controller) {
    const Command command = parse_easycomm(line);
    switch (command.kind) {
        case CommandKind::query_position: {
            const Position position = controller.position();
            return format_position(position.azimuth, position.elevation);
        }
        case CommandKind::status:
            return format_status_json(controller.status());
        case CommandKind::sensor_test:
            return format_status_json(controller.status());
        case CommandKind::sensor_calibrate_accel: {
            std::string error;
            if (!controller.request_sensor_action(SensorAction::calibrate_accelerometer, error)) {
                return "ERR " + error + "\r\n";
            }
            return "OK SENSOR CALIBRATE ACCEL QUEUED\r\n";
        }
        case CommandKind::sensor_calibrate_magnetic_start: {
            std::string error;
            if (!controller.request_sensor_action(SensorAction::magnetic_calibration_start, error)) {
                return "ERR " + error + "\r\n";
            }
            return "OK SENSOR CALIBRATE MAGNETIC START QUEUED\r\n";
        }
        case CommandKind::sensor_calibrate_magnetic_finish: {
            std::string error;
            if (!controller.request_sensor_action(SensorAction::magnetic_calibration_finish, error)) {
                return "ERR " + error + "\r\n";
            }
            return "OK SENSOR CALIBRATE MAGNETIC FINISH QUEUED\r\n";
        }
        case CommandKind::set_position: {
            std::string error;
            if (!controller.set_target(command.azimuth, command.elevation, error)) {
                return "ERR " + error + "\r\n";
            }
            return "OK MOVE\r\n";
        }
        case CommandKind::stop:
            controller.stop();
            return "OK STOP\r\n";
        case CommandKind::zero:
            if (!controller.zero_current_position()) {
                return "ERR stop motion and obtain valid feedback before zeroing\r\n";
            }
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

void configure_client(int client) {
    const int enabled = 1;
    ::setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
    const int keep_idle_seconds = 30;
    const int keep_interval_seconds = 10;
    const int keep_count = 3;
    ::setsockopt(client, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle_seconds,
                 sizeof(keep_idle_seconds));
    ::setsockopt(client, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval_seconds,
                 sizeof(keep_interval_seconds));
    ::setsockopt(client, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count));
    timeval send_timeout{.tv_sec = 2, .tv_usec = 0};
    ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
}

void serve_client(int client, RotatorController& controller, std::atomic_bool& stopping) {
    configure_client(client);
    std::string pending;
    char buffer[512];
    while (!stopping.load()) {
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(client, &sockets);
        timeval timeout{.tv_sec = 1, .tv_usec = 0};
        const int ready = ::select(client + 1, &sockets, nullptr, nullptr, &timeout);
        if (ready == 0 || (ready < 0 && errno == EINTR)) {
            continue;
        }
        if (ready < 0) {
            return;
        }
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

struct ClientWorker {
    std::thread thread;
    std::shared_ptr<std::atomic_bool> complete;
};

void reap_clients(std::vector<ClientWorker>& clients, bool join_all = false) {
    auto client = clients.begin();
    while (client != clients.end()) {
        if (join_all || client->complete->load()) {
            client->thread.join();
            client = clients.erase(client);
        } else {
            ++client;
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
    std::vector<ClientWorker> clients;
    while (!stopping.load()) {
        reap_clients(clients);
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
                auto complete = std::make_shared<std::atomic_bool>(false);
                clients.push_back(ClientWorker{
                    .thread = std::thread([client, this, &stopping, complete] {
                        serve_client(client, controller_, stopping);
                        ::close(client);
                        complete->store(true);
                    }),
                    .complete = std::move(complete),
                });
            }
        }
    }
    ::close(server);
    reap_clients(clients, true);
    return 0;
}

}  // namespace rotator
