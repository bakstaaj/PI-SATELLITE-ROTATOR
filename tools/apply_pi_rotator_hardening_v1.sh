#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f CMakeLists.txt || ! -d src || ! -d include/rotator ]]; then
  echo 'Run this script from the PI-SATELLITE-ROTATOR repository root.' >&2
  exit 1
fi

echo 'Applying PI Satellite Rotator hardening patch v1...'

mkdir -p 'include/rotator'
cat > 'include/rotator/rotator.hpp' <<'PATCH_FILE_EOF'
#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

namespace rotator {

struct Position {
    double azimuth{0.0};
    double elevation{0.0};
    bool moving{false};
};

struct ControllerStatus {
    double azimuth{0.0};
    double elevation{0.0};
    double target_azimuth{0.0};
    double target_elevation{0.0};
    bool moving{false};
    bool external_feedback{false};
    bool feedback_received{false};
    bool feedback_stale{false};
    long long feedback_age_ms{-1};
    bool fault{false};
    std::string fault_reason;
};

class RotatorController {
public:
    bool set_target(std::optional<double> azimuth,
                    std::optional<double> elevation,
                    std::string& error);
    Position position() const;
    ControllerStatus status() const;
    void stop();
    bool zero_current_position();
    void enable_external_feedback();
    void set_feedback_timeout(std::chrono::milliseconds timeout);
    bool update_feedback(double azimuth, double elevation);

private:
    void update_motion_state_locked();
    bool feedback_stale_locked(std::chrono::steady_clock::time_point now) const;
    ControllerStatus status_locked(std::chrono::steady_clock::time_point now) const;

    mutable std::mutex mutex_;
    Position state_;
    Position target_;
    Position raw_feedback_;
    Position feedback_zero_;
    std::chrono::steady_clock::time_point last_feedback_time_{};
    std::chrono::milliseconds feedback_timeout_{1000};
    std::string last_feedback_error_;
    bool external_feedback_{false};
    bool feedback_received_{false};
    bool motion_commanded_{false};
};

}  // namespace rotator
PATCH_FILE_EOF

mkdir -p 'src'
cat > 'src/rotator.cpp' <<'PATCH_FILE_EOF'
#include "rotator/rotator.hpp"

#include <algorithm>
#include <cmath>

namespace rotator {
namespace {
double azimuth_error(double target, double actual) {
    double error = target - actual;
    while (error > 180.0) {
        error -= 360.0;
    }
    while (error < -180.0) {
        error += 360.0;
    }
    return error;
}

double normalize_azimuth(double value) {
    while (value >= 360.0) {
        value -= 360.0;
    }
    while (value < 0.0) {
        value += 360.0;
    }
    return value;
}
}  // namespace

bool RotatorController::set_target(std::optional<double> azimuth,
                                   std::optional<double> elevation,
                                   std::string& error) {
    if (azimuth && (*azimuth < 0.0 || *azimuth > 359.0)) {
        error = "azimuth must be between 0 and 359 degrees";
        return false;
    }
    if (elevation && (*elevation < 0.0 || *elevation > 180.0)) {
        error = "elevation must be between 0 and 180 degrees";
        return false;
    }

    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (external_feedback_ && !feedback_received_) {
        error = "valid sensor feedback is required before motion commands";
        return false;
    }
    if (feedback_stale_locked(now)) {
        error = "sensor feedback is stale";
        return false;
    }
    if (azimuth) {
        target_.azimuth = *azimuth;
    }
    if (elevation) {
        target_.elevation = *elevation;
    }
    motion_commanded_ = true;
    if (!external_feedback_) {
        state_.azimuth = target_.azimuth;
        state_.elevation = target_.elevation;
        motion_commanded_ = false;
    }
    update_motion_state_locked();
    return true;
}

Position RotatorController::position() const {
    std::lock_guard lock(mutex_);
    Position result = state_;
    if (feedback_stale_locked(std::chrono::steady_clock::now())) {
        result.moving = false;
    }
    return result;
}

ControllerStatus RotatorController::status() const {
    std::lock_guard lock(mutex_);
    return status_locked(std::chrono::steady_clock::now());
}

void RotatorController::stop() {
    std::lock_guard lock(mutex_);
    motion_commanded_ = false;
    state_.moving = false;
}

bool RotatorController::zero_current_position() {
    std::lock_guard lock(mutex_);
    if (state_.moving || (external_feedback_ && !feedback_received_) ||
        feedback_stale_locked(std::chrono::steady_clock::now())) {
        return false;
    }
    motion_commanded_ = false;
    if (external_feedback_) {
        feedback_zero_.azimuth = raw_feedback_.azimuth;
        feedback_zero_.elevation = raw_feedback_.elevation;
    }
    state_.azimuth = 0.0;
    state_.elevation = 0.0;
    state_.moving = false;
    target_ = state_;
    return true;
}

void RotatorController::enable_external_feedback() {
    std::lock_guard lock(mutex_);
    external_feedback_ = true;
    motion_commanded_ = false;
    state_.moving = false;
    feedback_received_ = false;
    last_feedback_error_.clear();
}

void RotatorController::set_feedback_timeout(std::chrono::milliseconds timeout) {
    std::lock_guard lock(mutex_);
    feedback_timeout_ = timeout.count() > 0 ? timeout : std::chrono::milliseconds(1000);
}

bool RotatorController::update_feedback(double azimuth, double elevation) {
    if (azimuth < 0.0 || azimuth >= 360.0 || elevation < 0.0 || elevation > 180.0) {
        std::lock_guard lock(mutex_);
        last_feedback_error_ = "feedback outside sensor range";
        return false;
    }

    std::lock_guard lock(mutex_);
    const double mapped_azimuth = normalize_azimuth(azimuth - feedback_zero_.azimuth);
    const double mapped_elevation = elevation - feedback_zero_.elevation;
    constexpr double boundary_tolerance = 1.0;
    if (mapped_elevation < -boundary_tolerance ||
        mapped_elevation > 180.0 + boundary_tolerance) {
        last_feedback_error_ = "mapped elevation outside 0-180 degrees";
        return false;
    }

    raw_feedback_.azimuth = azimuth;
    raw_feedback_.elevation = elevation;
    feedback_received_ = true;
    last_feedback_time_ = std::chrono::steady_clock::now();
    last_feedback_error_.clear();
    state_.azimuth = mapped_azimuth;
    state_.elevation = std::clamp(mapped_elevation, 0.0, 180.0);
    update_motion_state_locked();
    return true;
}

void RotatorController::update_motion_state_locked() {
    constexpr double deadband = 0.5;
    state_.moving = external_feedback_ && motion_commanded_ && feedback_received_ &&
                    !feedback_stale_locked(std::chrono::steady_clock::now()) &&
                    (std::abs(azimuth_error(target_.azimuth, state_.azimuth)) > deadband ||
                     std::abs(target_.elevation - state_.elevation) > deadband);
}

bool RotatorController::feedback_stale_locked(
    std::chrono::steady_clock::time_point now) const {
    return external_feedback_ && feedback_received_ &&
           now - last_feedback_time_ > feedback_timeout_;
}

ControllerStatus RotatorController::status_locked(
    std::chrono::steady_clock::time_point now) const {
    ControllerStatus status;
    status.azimuth = state_.azimuth;
    status.elevation = state_.elevation;
    status.target_azimuth = target_.azimuth;
    status.target_elevation = target_.elevation;
    status.external_feedback = external_feedback_;
    status.feedback_received = feedback_received_;
    status.feedback_stale = feedback_stale_locked(now);
    status.moving = state_.moving && !status.feedback_stale;
    if (feedback_received_) {
        status.feedback_age_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_feedback_time_)
                .count();
    }
    status.fault = status.feedback_stale;
    if (status.feedback_stale) {
        status.fault_reason = "stale sensor feedback";
    } else {
        status.fault_reason = last_feedback_error_;
    }
    return status;
}

}  // namespace rotator
PATCH_FILE_EOF

mkdir -p 'include/rotator'
cat > 'include/rotator/easycomm.hpp' <<'PATCH_FILE_EOF'
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rotator {

enum class CommandKind { set_position, query_position, status, stop, zero, park, invalid };

struct Command {
    CommandKind kind{CommandKind::invalid};
    std::optional<double> azimuth;
    std::optional<double> elevation;
    std::string error;
};

Command parse_easycomm(std::string_view line);
std::string format_position(double azimuth, double elevation);

}  // namespace rotator
PATCH_FILE_EOF

mkdir -p 'src'
cat > 'src/easycomm.cpp' <<'PATCH_FILE_EOF'
#include "rotator/easycomm.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace rotator {
namespace {

std::string uppercase_trimmed(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    std::string result(input.substr(first, last - first + 1));
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

bool parse_number(std::string_view text, double& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end && std::isfinite(value);
}

}  // namespace

Command parse_easycomm(std::string_view line) {
    const std::string text = uppercase_trimmed(line);
    if (text.empty()) {
        return {CommandKind::invalid, std::nullopt, std::nullopt, "empty command"};
    }
    if (text == "SA" || text == "SE" || text == "SA SE" || text == "STOP") {
        return {CommandKind::stop, std::nullopt, std::nullopt, {}};
    }
    if (text == "ZERO") {
        return {CommandKind::zero, std::nullopt, std::nullopt, {}};
    }
    if (text == "PARK") {
        return {CommandKind::park, std::nullopt, std::nullopt, {}};
    }
    if (text == "STATUS") {
        return {CommandKind::status, std::nullopt, std::nullopt, {}};
    }

    std::istringstream input(text);
    std::vector<std::string> tokens;
    for (std::string token; input >> token;) {
        tokens.push_back(token);
    }

    if ((tokens.size() == 2 && tokens[0] == "AZ" && tokens[1] == "EL") ||
        (tokens.size() == 1 && (tokens[0] == "AZ" || tokens[0] == "EL"))) {
        return {CommandKind::query_position, std::nullopt, std::nullopt, {}};
    }

    Command command{CommandKind::set_position, std::nullopt, std::nullopt, {}};
    for (const auto& token : tokens) {
        std::optional<double>* destination = nullptr;
        std::string_view number;
        if (token.starts_with("AZ")) {
            destination = &command.azimuth;
            number = std::string_view(token).substr(2);
        } else if (token.starts_with("EL")) {
            destination = &command.elevation;
            number = std::string_view(token).substr(2);
        } else {
            return {CommandKind::invalid, std::nullopt, std::nullopt,
                    "unsupported token: " + token};
        }
        if (*destination) {
            return {CommandKind::invalid, std::nullopt, std::nullopt,
                    "duplicate axis token"};
        }
        double value = 0.0;
        if (!parse_number(number, value)) {
            return {CommandKind::invalid, std::nullopt, std::nullopt,
                    "invalid axis value"};
        }
        *destination = value;
    }

    if (!command.azimuth && !command.elevation) {
        return {CommandKind::invalid, std::nullopt, std::nullopt,
                "no axis value supplied"};
    }
    return command;
}

std::string format_position(double azimuth, double elevation) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << "AZ" << azimuth << " EL" << elevation
           << "\r\n";
    return output.str();
}

}  // namespace rotator
PATCH_FILE_EOF

mkdir -p 'src'
cat > 'src/tcp_server.cpp' <<'PATCH_FILE_EOF'
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
PATCH_FILE_EOF

mkdir -p 'src'
cat > 'src/web_server.cpp' <<'PATCH_FILE_EOF'
#include "rotator/web_server.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <netdb.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <utility>

namespace rotator {
namespace {

constexpr std::string_view page = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Satellite Rotator</title>
<style>
:root{color-scheme:dark;--bg:#081019;--panel:#111d29;--line:#26394b;--text:#ecf4fa;--muted:#8fa7b9;--cyan:#35d0e5;--green:#62d68b;--amber:#f4ba52;--red:#ff626d}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 50% -20%,#18334a 0,var(--bg) 48%);color:var(--text);font:16px system-ui,-apple-system,Segoe UI,sans-serif;min-height:100vh}
.shell{width:min(980px,calc(100% - 28px));margin:auto;padding:28px 0 44px}.top{display:flex;align-items:center;justify-content:space-between;gap:18px;margin-bottom:20px}
h1{font-size:clamp(1.3rem,4vw,2rem);margin:0;letter-spacing:.04em}.eyebrow{color:var(--cyan);font:700 .72rem ui-monospace,monospace;letter-spacing:.2em;text-transform:uppercase;margin-bottom:5px}
.status{display:flex;align-items:center;gap:9px;color:var(--muted);font-size:.86rem}.dot{width:10px;height:10px;border-radius:50%;background:var(--amber);box-shadow:0 0 14px var(--amber)}.online .dot{background:var(--green);box-shadow:0 0 14px var(--green)}.offline .dot{background:var(--red);box-shadow:0 0 14px var(--red)}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:16px}.card{background:linear-gradient(145deg,rgba(20,35,49,.96),rgba(12,23,33,.96));border:1px solid var(--line);border-radius:16px;padding:20px;box-shadow:0 14px 35px #0005}
.readout{display:flex;justify-content:space-between;align-items:end}.label{color:var(--muted);font-size:.78rem;letter-spacing:.15em;text-transform:uppercase}.angle{font:600 clamp(2.6rem,9vw,5rem)/1 ui-monospace,monospace;letter-spacing:-.08em}.unit{font-size:1rem;color:var(--cyan);margin:0 0 8px 8px}.track{height:7px;background:#07111a;border-radius:9px;overflow:hidden;margin-top:20px}.fill{height:100%;width:0;background:linear-gradient(90deg,var(--cyan),var(--green));transition:width .25s ease}
.controls{margin-top:16px}.target-grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}.field label{display:flex;justify-content:space-between;color:var(--muted);font-size:.84rem;margin-bottom:8px}.field input[type=number]{width:92px;background:#07111a;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px}.field input[type=range]{width:100%;accent-color:var(--cyan)}
.actions{display:grid;grid-template-columns:2fr repeat(3,1fr);gap:10px;margin-top:20px}button{border:1px solid var(--line);border-radius:10px;background:#182839;color:var(--text);padding:12px 14px;font:700 .8rem system-ui;letter-spacing:.06em;text-transform:uppercase;cursor:pointer}button:hover{filter:brightness(1.18)}button:active{transform:translateY(1px)}button.primary{background:var(--cyan);border-color:var(--cyan);color:#041116}button.stop{background:#401a22;border-color:#7f2c39;color:#ffadb4}button:disabled{opacity:.4;cursor:not-allowed}
.jogs{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:16px}.jog-group{display:grid;grid-template-columns:repeat(4,1fr);gap:7px}.jog-title{grid-column:1/-1;color:var(--muted);font-size:.78rem}.log{margin-top:16px;color:var(--muted);font:13px ui-monospace,monospace;min-height:1.4em}.danger{color:var(--red)}
@media(max-width:700px){.grid,.target-grid,.jogs{grid-template-columns:1fr}.actions{grid-template-columns:1fr 1fr}.actions .primary,.actions .stop{grid-column:span 1}.card{padding:16px}.shell{padding-top:18px}}
</style>
</head>
<body><main class="shell">
<header class="top"><div><div class="eyebrow">EasyComm control surface</div><h1>Satellite Rotator</h1></div><div id="status" class="status"><span class="dot"></span><span id="statusText">Connecting</span></div></header>
<section class="grid">
<article class="card"><div class="readout"><div><div class="label">Azimuth</div><span id="azNow" class="angle">---.-</span><span class="unit">deg</span></div></div><div class="track"><div id="azFill" class="fill"></div></div></article>
<article class="card"><div class="readout"><div><div class="label">Elevation</div><span id="elNow" class="angle">---.-</span><span class="unit">deg</span></div></div><div class="track"><div id="elFill" class="fill"></div></div></article>
</section>
<section class="card controls">
<div class="target-grid">
<div class="field"><label><span>Target azimuth</span><input id="azTarget" type="number" min="0" max="359" step="0.1" value="0.0"></label><input id="azSlider" type="range" min="0" max="359" step="0.1" value="0"></div>
<div class="field"><label><span>Target elevation</span><input id="elTarget" type="number" min="0" max="180" step="0.1" value="0.0"></label><input id="elSlider" type="range" min="0" max="180" step="0.1" value="0"></div>
</div>
<div class="actions"><button class="primary" data-control id="move">Move</button><button class="stop" data-control id="stop">Stop</button><button data-control id="zero">Zero here</button><button data-control id="park">Park 0/0</button></div>
<div class="jogs"><div class="jog-group"><div class="jog-title">Azimuth jog</div><button data-control data-axis="az" data-delta="-5">-5</button><button data-control data-axis="az" data-delta="-1">-1</button><button data-control data-axis="az" data-delta="1">+1</button><button data-control data-axis="az" data-delta="5">+5</button></div><div class="jog-group"><div class="jog-title">Elevation jog</div><button data-control data-axis="el" data-delta="-5">-5</button><button data-control data-axis="el" data-delta="-1">-1</button><button data-control data-axis="el" data-delta="1">+1</button><button data-control data-axis="el" data-delta="5">+5</button></div></div>
<div id="log" class="log">Waiting for EasyComm listener...</div>
</section>
</main>
<script>
const $=id=>document.getElementById(id);let current={azimuth:0,elevation:0},busy=false;
function sync(a,b){$(a).addEventListener('input',()=>$(b).value=$(a).value);$(b).addEventListener('input',()=>$(a).value=$(b).value)}sync('azTarget','azSlider');sync('elTarget','elSlider');
function connected(ok,msg){$('status').className='status '+(ok?'online':'offline');$('statusText').textContent=ok?'EasyComm online':'Offline';document.querySelectorAll('[data-control]').forEach(x=>x.disabled=!ok||busy);$('log').textContent=msg||'';$('log').className='log '+(ok?'':'danger')}
async function api(path,method='GET'){const r=await fetch(path,{method,cache:'no-store'});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.error||('HTTP '+r.status));return j}
function statusLine(j){if(j.fault)return 'FAULT: '+(j.fault_reason||'unknown');let s=j.moving?'Moving':'Idle';if(j.external_feedback){s+=' | sensor '+(j.feedback_received?(j.feedback_age_ms+' ms old'):'waiting for first frame');}return s+' | backend '+j.backend}
async function refresh(){try{const j=await api('/api/status');current=j;$('azNow').textContent=j.azimuth.toFixed(1);$('elNow').textContent=j.elevation.toFixed(1);$('azFill').style.width=(j.azimuth/359*100)+'%';$('elFill').style.width=(j.elevation/180*100)+'%';connected(!j.fault,statusLine(j))}catch(e){connected(false,e.message)}}
async function command(path,label){busy=true;connected(true,label);try{await api(path,'POST');await refresh()}catch(e){connected(false,e.message)}finally{busy=false;document.querySelectorAll('[data-control]').forEach(x=>x.disabled=$('status').classList.contains('offline'))}}
$('move').onclick=()=>command('/api/move?az='+encodeURIComponent($('azTarget').value)+'&el='+encodeURIComponent($('elTarget').value),'Sending EasyComm target...');
$('stop').onclick=()=>command('/api/stop','Sending stop...');
$('park').onclick=()=>command('/api/park','Sending park target...');
$('zero').onclick=()=>{if(confirm('Set the current sensor position to azimuth 0 and elevation 0?'))command('/api/zero','Zeroing current position...')};
document.querySelectorAll('[data-axis]').forEach(b=>b.onclick=()=>{let az=current.azimuth,el=current.elevation,d=Number(b.dataset.delta);if(b.dataset.axis==='az')az=(az+d+360)%360;else el=Math.max(0,Math.min(180,el+d));$('azTarget').value=$('azSlider').value=az.toFixed(1);$('elTarget').value=$('elSlider').value=el.toFixed(1);command('/api/move?az='+az.toFixed(1)+'&el='+el.toFixed(1),'Sending jog...')});
refresh();setInterval(refresh,750);
</script></body></html>)HTML";

struct HttpRequest {
    std::string method;
    std::string target;
};

bool send_all(int socket, std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto count = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
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

void http_response(int client, int status, std::string_view content_type,
                   const std::string& body) {
    const std::string reason = status == 200 ? "OK" : (status == 404 ? "Not Found" : "Bad Request");
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Cache-Control: no-store\r\n"
             << "X-Content-Type-Options: nosniff\r\n"
             << "Content-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'\r\n"
             << "Connection: close\r\n\r\n" << body;
    send_all(client, response.str());
}

bool wait_readable(int socket, int timeout_seconds) {
    fd_set sockets;
    FD_ZERO(&sockets);
    FD_SET(socket, &sockets);
    timeval timeout{.tv_sec = timeout_seconds, .tv_usec = 0};
    const int ready = ::select(socket + 1, &sockets, nullptr, nullptr, &timeout);
    return ready > 0;
}

std::optional<HttpRequest> read_request(int client) {
    std::string data;
    char buffer[1024];
    while (data.find("\r\n\r\n") == std::string::npos && data.size() < 8192) {
        if (!wait_readable(client, 2)) {
            return std::nullopt;
        }
        const auto count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            return std::nullopt;
        }
        data.append(buffer, static_cast<std::size_t>(count));
    }
    const auto line_end = data.find("\r\n");
    if (line_end == std::string::npos) {
        return std::nullopt;
    }
    std::istringstream line(data.substr(0, line_end));
    HttpRequest request;
    std::string version;
    if (!(line >> request.method >> request.target >> version) ||
        !version.starts_with("HTTP/1.")) {
        return std::nullopt;
    }
    return request;
}

std::string connect_easycomm(const std::string& host, std::uint16_t port,
                             std::string_view command, bool read_response) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    const int lookup = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (lookup != 0) {
        throw std::runtime_error("EasyComm address lookup failed: " +
                                 std::string(::gai_strerror(lookup)));
    }

    int socket = -1;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket >= 0 && ::connect(socket, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        if (socket >= 0) {
            ::close(socket);
            socket = -1;
        }
    }
    ::freeaddrinfo(addresses);
    if (socket < 0) {
        throw std::runtime_error("cannot connect to EasyComm listener " + host + ':' + service);
    }
    timeval timeout{.tv_sec = 2, .tv_usec = 0};
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (!send_all(socket, command)) {
        ::close(socket);
        throw std::runtime_error("failed to send EasyComm command");
    }
    std::string response;
    if (read_response) {
        char buffer[256];
        while (response.find('\n') == std::string::npos && response.size() < 1024) {
            const auto count = ::recv(socket, buffer, sizeof(buffer), 0);
            if (count <= 0) {
                break;
            }
            response.append(buffer, static_cast<std::size_t>(count));
        }
    }
    ::close(socket);
    if (read_response && response.empty()) {
        throw std::runtime_error("EasyComm listener returned no response");
    }
    return response;
}

void require_easycomm_ok(std::string_view response) {
    if (response.starts_with("ERR")) {
        throw std::runtime_error(std::string(response));
    }
}

std::optional<double> query_number(std::string_view target, std::string_view key) {
    const auto question = target.find('?');
    if (question == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view query = target.substr(question + 1);
    while (!query.empty()) {
        const auto separator = query.find('&');
        const auto item = query.substr(0, separator);
        const auto equal = item.find('=');
        if (equal != std::string_view::npos && item.substr(0, equal) == key) {
            const auto text = item.substr(equal + 1);
            double value = 0.0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                std::isfinite(value)) {
                return value;
            }
            return std::nullopt;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        query.remove_prefix(separator + 1);
    }
    return std::nullopt;
}

void api_error(int client, const std::exception& error) {
    http_response(client, 400, "application/json; charset=utf-8",
                  "{\"ok\":false,\"error\":\"" + json_escape(error.what()) + "\"}");
}

void handle_request(int client, const HttpRequest& request, const std::string& host,
                    std::uint16_t port) {
    try {
        if (request.method == "GET" && (request.target == "/" || request.target == "/index.html")) {
            http_response(client, 200, "text/html; charset=utf-8", std::string(page));
            return;
        }
        if (request.method == "GET" && request.target == "/api/status") {
            const auto raw = connect_easycomm(host, port, "STATUS\n", true);
            if (!raw.starts_with("{")) {
                throw std::runtime_error("invalid EasyComm status response");
            }
            http_response(client, 200, "application/json; charset=utf-8", raw);
            return;
        }
        if (request.method == "POST" && request.target.starts_with("/api/move?")) {
            const auto azimuth = query_number(request.target, "az");
            const auto elevation = query_number(request.target, "el");
            if (!azimuth || !elevation || *azimuth < 0.0 || *azimuth > 359.0 ||
                *elevation < 0.0 || *elevation > 180.0) {
                throw std::runtime_error("target must be AZ 0-359 and EL 0-180");
            }
            std::ostringstream command;
            command << std::fixed << std::setprecision(1) << "AZ" << *azimuth << " EL"
                    << *elevation << '\n';
            require_easycomm_ok(connect_easycomm(host, port, command.str(), true));
            http_response(client, 200, "application/json", "{\"ok\":true}");
            return;
        }
        if (request.method == "POST" && request.target == "/api/stop") {
            require_easycomm_ok(connect_easycomm(host, port, "SA SE\n", true));
            http_response(client, 200, "application/json", "{\"ok\":true}");
            return;
        }
        if (request.method == "POST" && request.target == "/api/zero") {
            require_easycomm_ok(connect_easycomm(host, port, "ZERO\n", true));
            http_response(client, 200, "application/json", "{\"ok\":true}");
            return;
        }
        if (request.method == "POST" && request.target == "/api/park") {
            require_easycomm_ok(connect_easycomm(host, port, "PARK\n", true));
            http_response(client, 200, "application/json", "{\"ok\":true}");
            return;
        }
        http_response(client, 404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
    } catch (const std::exception& error) {
        api_error(client, error);
    }
}

}  // namespace

WebServer::WebServer(std::uint16_t http_port, std::string easycomm_host,
                     std::uint16_t easycomm_port)
    : http_port_(http_port),
      easycomm_host_(std::move(easycomm_host)),
      easycomm_port_(easycomm_port) {}

int WebServer::run(std::atomic_bool& stopping) {
    const int server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "web socket: " << std::strerror(errno) << '\n';
        return 1;
    }
    const int reuse = 1;
    ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(http_port_);
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(server, 8) < 0) {
        std::cerr << "web listen: " << std::strerror(errno) << '\n';
        ::close(server);
        return 1;
    }
    std::cout << "Rotator web control listening on HTTP port " << http_port_ << '\n';
    while (!stopping.load()) {
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(server, &sockets);
        timeval timeout{.tv_sec = 1, .tv_usec = 0};
        const int ready = ::select(server + 1, &sockets, nullptr, nullptr, &timeout);
        if (ready < 0 && errno != EINTR) {
            std::cerr << "web select: " << std::strerror(errno) << '\n';
            break;
        }
        if (ready > 0) {
            const int client = ::accept(server, nullptr, nullptr);
            if (client >= 0) {
                if (const auto request = read_request(client)) {
                    handle_request(client, *request, easycomm_host_, easycomm_port_);
                } else {
                    http_response(client, 400, "application/json",
                                  "{\"ok\":false,\"error\":\"bad request\"}");
                }
                ::close(client);
            }
        }
    }
    ::close(server);
    return 0;
}

}  // namespace rotator
PATCH_FILE_EOF

mkdir -p 'src'
cat > 'src/main.cpp' <<'PATCH_FILE_EOF'
#include "rotator/tcp_server.hpp"
#include "rotator/web_server.hpp"
#include "rotator/witmotion.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
std::atomic_bool stopping{false};
void handle_signal(int) { stopping.store(true); }

enum class SensorAxis : std::size_t { roll = 0, pitch = 1, yaw = 2 };

struct Options {
    std::uint16_t port{4553};
    std::uint16_t web_port{8080};
    bool web_enabled{true};
    std::string sensor_device;
    unsigned int sensor_baud{115200};
    unsigned int feedback_timeout_ms{1000};
    SensorAxis azimuth_axis{SensorAxis::yaw};
    SensorAxis elevation_axis{SensorAxis::roll};
    double azimuth_offset{0.0};
    double elevation_offset{0.0};
    double azimuth_sign{1.0};
    double elevation_sign{1.0};
};

SensorAxis parse_axis(const std::string& value) {
    if (value == "roll") {
        return SensorAxis::roll;
    }
    if (value == "pitch") {
        return SensorAxis::pitch;
    }
    if (value == "yaw") {
        return SensorAxis::yaw;
    }
    throw std::invalid_argument("axis must be roll, pitch, or yaw");
}

int integer(const char* value, const std::string& name) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != std::string(value).size()) {
            throw std::invalid_argument(name);
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid " + name + ": " + value);
    }
}

double number(const char* value, const std::string& name) {
    try {
        std::size_t used = 0;
        const double parsed = std::stod(value, &used);
        if (used != std::string(value).size()) {
            throw std::invalid_argument(name);
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid " + name + ": " + value);
    }
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc) {
            const int value = integer(argv[++i], "TCP port");
            if (value < 1 || value > 65535) {
                throw std::invalid_argument("TCP port must be 1-65535");
            }
            options.port = static_cast<std::uint16_t>(value);
        } else if (argument == "--web-port" && i + 1 < argc) {
            const int value = integer(argv[++i], "web port");
            if (value < 1 || value > 65535) {
                throw std::invalid_argument("web port must be 1-65535");
            }
            options.web_port = static_cast<std::uint16_t>(value);
        } else if (argument == "--no-web") {
            options.web_enabled = false;
        } else if (argument == "--sensor" && i + 1 < argc) {
            options.sensor_device = argv[++i];
        } else if (argument == "--sensor-baud" && i + 1 < argc) {
            const int value = integer(argv[++i], "sensor baud rate");
            if (value <= 0) {
                throw std::invalid_argument("sensor baud rate must be positive");
            }
            options.sensor_baud = static_cast<unsigned int>(value);
        } else if (argument == "--feedback-timeout-ms" && i + 1 < argc) {
            const int value = integer(argv[++i], "feedback timeout");
            if (value <= 0) {
                throw std::invalid_argument("feedback timeout must be positive");
            }
            options.feedback_timeout_ms = static_cast<unsigned int>(value);
        } else if (argument == "--az-axis" && i + 1 < argc) {
            options.azimuth_axis = parse_axis(argv[++i]);
        } else if (argument == "--el-axis" && i + 1 < argc) {
            options.elevation_axis = parse_axis(argv[++i]);
        } else if (argument == "--az-offset" && i + 1 < argc) {
            options.azimuth_offset = number(argv[++i], "azimuth offset");
        } else if (argument == "--el-offset" && i + 1 < argc) {
            options.elevation_offset = number(argv[++i], "elevation offset");
        } else if (argument == "--az-invert") {
            options.azimuth_sign = -1.0;
        } else if (argument == "--el-invert") {
            options.elevation_sign = -1.0;
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + argument);
        }
    }
    return options;
}

void print_usage() {
    std::cerr
        << "usage: pi-satellite-rotator [--port PORT] [--sensor DEVICE]\n"
        << "       [--web-port PORT] [--no-web]\n"
        << "       [--sensor-baud RATE] [--feedback-timeout-ms MS]\n"
        << "       [--az-axis roll|pitch|yaw] [--el-axis roll|pitch|yaw]\n"
        << "       [--az-offset DEG] [--el-offset DEG]\n"
        << "       [--az-invert] [--el-invert]\n";
}

void read_sensor(rotator::SerialPort port, const Options& options,
                 rotator::RotatorController& controller) {
    try {
        rotator::WitFrameDecoder decoder;
        rotator::WitSample sample;
        std::array<std::uint8_t, 256> bytes{};
        std::size_t dropped_elevation_frames = 0;
        bool elevation_mapping_valid = true;
        auto next_mapping_warning = std::chrono::steady_clock::now();
        while (!stopping.load()) {
            const auto count = port.read(bytes, std::chrono::milliseconds(200));
            for (const auto& frame : decoder.push(std::span(bytes).first(count))) {
                if (!rotator::apply_wit_frame(frame, sample) ||
                    (frame.type != static_cast<std::uint8_t>(rotator::WitFrameType::angle) &&
                     frame.type !=
                         static_cast<std::uint8_t>(rotator::WitFrameType::combined_motion))) {
                    continue;
                }
                const double azimuth = rotator::normalize_azimuth(
                    options.azimuth_sign *
                        sample.angle_degrees[static_cast<std::size_t>(options.azimuth_axis)] +
                    options.azimuth_offset);
                const double elevation =
                    options.elevation_sign *
                        sample.angle_degrees[static_cast<std::size_t>(options.elevation_axis)] +
                    options.elevation_offset;
                if (!controller.update_feedback(azimuth, elevation)) {
                    ++dropped_elevation_frames;
                    elevation_mapping_valid = false;
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= next_mapping_warning) {
                        std::cerr << "WT901 elevation mapping is outside 0-180 degrees; dropped "
                                  << dropped_elevation_frames
                                  << " frame(s). Check --el-axis, --el-offset, and --el-invert\n";
                        dropped_elevation_frames = 0;
                        next_mapping_warning = now + std::chrono::seconds(10);
                    }
                } else if (!elevation_mapping_valid) {
                    std::cerr << "WT901 elevation mapping recovered\n";
                    elevation_mapping_valid = true;
                    dropped_elevation_frames = 0;
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "WT901 reader stopped: " << error.what() << '\n';
        controller.stop();
        stopping.store(true);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        rotator::RotatorController controller;
        controller.set_feedback_timeout(std::chrono::milliseconds(options.feedback_timeout_ms));
        std::thread sensor_thread;
        std::thread web_thread;
        if (!options.sensor_device.empty()) {
            rotator::SerialPort sensor(options.sensor_device, options.sensor_baud);
            controller.enable_external_feedback();
            sensor_thread = std::thread(read_sensor, std::move(sensor), std::cref(options),
                                        std::ref(controller));
            std::cout << "WT901 input: " << options.sensor_device << " at "
                      << options.sensor_baud << " baud; feedback timeout "
                      << options.feedback_timeout_ms << " ms\n";
        }

        if (options.web_enabled) {
            web_thread = std::thread([&options] {
                rotator::WebServer web(options.web_port, "127.0.0.1", options.port);
                if (web.run(stopping) != 0) {
                    stopping.store(true);
                }
            });
        }

        rotator::TcpServer server(options.port, controller);
        const int result = server.run(stopping);
        stopping.store(true);
        if (sensor_thread.joinable()) {
            sensor_thread.join();
        }
        if (web_thread.joinable()) {
            web_thread.join();
        }
        return result;
    } catch (const std::exception& error) {
        std::cerr << "pi-satellite-rotator: " << error.what() << '\n';
        print_usage();
        return 2;
    }
}
PATCH_FILE_EOF

mkdir -p 'tests'
cat > 'tests/test_easycomm.cpp' <<'PATCH_FILE_EOF'
#include "rotator/easycomm.hpp"
#include "rotator/rotator.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace rotator;
    using namespace std::chrono_literals;

    const auto set = parse_easycomm("AZ123.4 EL45.6\r\n");
    require(set.kind == CommandKind::set_position, "set command kind");
    require(set.azimuth == 123.4 && set.elevation == 45.6, "set values");

    require(parse_easycomm("AZ EL").kind == CommandKind::query_position, "position query");
    require(parse_easycomm("STATUS").kind == CommandKind::status, "status command");
    require(parse_easycomm("SA SE").kind == CommandKind::stop, "stop command");
    require(parse_easycomm("ZERO").kind == CommandKind::zero, "zero command");
    require(parse_easycomm("PARK").kind == CommandKind::park, "park command");
    require(parse_easycomm("AZnope").kind == CommandKind::invalid, "reject malformed value");
    require(parse_easycomm("AZ1 AZ2").error == "duplicate axis token",
            "identify duplicate axis token");

    RotatorController controller;
    std::string error;
    require(controller.set_target(359.0, 180.0, error), "accept limits");
    require(!controller.set_target(359.1, std::nullopt, error), "reject azimuth above limit");
    require(!controller.set_target(std::nullopt, -0.1, error), "reject negative elevation");
    require(format_position(12.34, 5.67) == "AZ12.3 EL5.7\r\n", "format response");

    const auto simulator_status = controller.status();
    require(!simulator_status.external_feedback, "simulator reports no external feedback");
    require(simulator_status.target_azimuth == 359.0, "status reports target azimuth");

    controller.enable_external_feedback();
    require(!controller.set_target(125.0, 35.0, error),
            "reject external motion before feedback");
    require(controller.update_feedback(120.0, 35.0), "accept external feedback");
    require(controller.set_target(125.0, 35.0, error), "set external target");
    require(controller.position().moving, "external target reports motion needed");
    controller.stop();
    require(!controller.position().moving, "stop clears motion state");
    require(!controller.update_feedback(120.0, 181.0), "reject invalid feedback");
    require(controller.zero_current_position(), "zero stopped controller");
    require(controller.position().azimuth == 0.0 && controller.position().elevation == 0.0,
            "zero current position");
    require(controller.update_feedback(130.0, 40.0), "feedback after zero");
    require(controller.position().azimuth == 10.0 && controller.position().elevation == 5.0,
            "zero offsets external feedback");
    require(controller.set_target(140.0, 45.0, error), "set moving target before zero test");
    require(!controller.zero_current_position(), "reject zero while moving");
    controller.stop();
    require(controller.zero_current_position(), "allow zero after stop");

    RotatorController stale_controller;
    stale_controller.enable_external_feedback();
    stale_controller.set_feedback_timeout(10ms);
    require(stale_controller.update_feedback(20.0, 20.0), "seed stale feedback test");
    require(!stale_controller.status().feedback_stale, "fresh feedback is not stale");
    std::this_thread::sleep_for(25ms);
    require(stale_controller.status().feedback_stale, "feedback becomes stale");
    require(stale_controller.status().fault, "stale feedback reports fault");
    require(!stale_controller.set_target(25.0, 25.0, error),
            "reject target when feedback is stale");

    RotatorController boundary_controller;
    boundary_controller.enable_external_feedback();
    require(!boundary_controller.zero_current_position(), "reject zero before first feedback");
    require(boundary_controller.update_feedback(10.0, 10.0), "seed boundary feedback");
    require(boundary_controller.zero_current_position(), "zero boundary controller");
    require(boundary_controller.update_feedback(10.0, 9.5), "tolerate elevation noise at zero");
    require(boundary_controller.position().elevation == 0.0, "clamp boundary noise to zero");
    require(!boundary_controller.update_feedback(10.0, 8.5), "reject material boundary error");
    require(boundary_controller.status().fault_reason == "mapped elevation outside 0-180 degrees",
            "report rejected feedback reason");
    require(boundary_controller.position().elevation == 0.0,
            "rejected feedback does not update position");

    std::cout << "All tests passed\n";
    return 0;
}
PATCH_FILE_EOF

mkdir -p 'tests'
cat > 'tests/test_web.cpp' <<'PATCH_FILE_EOF'
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

    const int stalled_easycomm_client = open_stalled_client(easycomm_port);

    const auto page = request(web_port, "GET", "/");
    require(page.find("200 OK") != std::string::npos, "serve control page");
    require(page.find("Satellite Rotator") != std::string::npos, "page title");

    const auto initial = request(web_port, "GET", "/api/status");
    require(initial.find("\"azimuth\":0.0") != std::string::npos, "initial azimuth");
    require(initial.find("\"elevation\":0.0") != std::string::npos, "initial elevation");
    require(initial.find("\"target_azimuth\":0.0") != std::string::npos,
            "initial target azimuth");
    require(initial.find("\"moving\":false") != std::string::npos,
            "initial moving state");
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

    ::close(stalled_easycomm_client);
    stopping.store(true);
    easycomm_thread.join();
    web_thread.join();
    std::cout << "All web integration tests passed\n";
    return 0;
}
PATCH_FILE_EOF

mkdir -p 'docs'
cat > 'docs/web-control.md' <<'PATCH_FILE_EOF'
# Web control panel

The rotator executable starts two listeners by default:

- EasyComm TCP: port 4553
- Web control: HTTP port 8080

Start in simulator mode:

```bash
./pi-satellite-rotator
```

Or start with live WT901 feedback:

```bash
./pi-satellite-rotator --sensor /dev/ttyUSB0
```

From another computer on the same network, open:

```text
http://PI_ADDRESS:8080/
```

## Controls

- **Move** sends `AZaaa.a ELbbb.b` through EasyComm and expects `OK MOVE`.
- **Jog** calculates a new target from the latest EasyComm status and sends a normal move command.
- **Stop** sends `SA SE` and expects `OK STOP`.
- **Zero here** sends `ZERO`; the current feedback becomes 0 degrees azimuth and 0 degrees elevation for this process lifetime. Zeroing is rejected while motion is commanded, before the first live feedback sample, or when live feedback is stale; press **Stop** first.
- **Park 0/0** sends `PARK`, which requests azimuth 0 and elevation 0.
- Position cards poll `STATUS` through EasyComm every 750 milliseconds.

In simulator mode, move and park targets are reached immediately. With live WT901 feedback but no motor backend, commands are accepted only after current feedback is available and not stale. The displayed position changes only when the sensor physically moves. This is intentional and allows the complete browser-to-EasyComm path to be tested before motors are energized.

## Status and stale-feedback protection

The web status endpoint proxies the non-standard EasyComm `STATUS` command. It returns JSON fields for current azimuth/elevation, target azimuth/elevation, moving state, external-feedback state, feedback age, stale-feedback state, and the current fault reason.

When external feedback is enabled, motion and zeroing commands are rejected until the WT901 has produced at least one valid frame. After that, feedback older than the configured timeout is reported as a fault and motion commands are rejected until fresh feedback arrives.

## Options

```text
--port PORT                EasyComm port, default 4553
--web-port PORT            HTTP port, default 8080
--no-web                   disable the HTTP server
--feedback-timeout-ms MS   stale-feedback threshold, default 1000
```

## Network safety

The web interface binds to all network interfaces and currently has no authentication or TLS. Use it only on a trusted private network, restrict port 8080 with the Pi firewall, and do not expose it through router port forwarding. The physical emergency stop remains required whenever motor power is enabled.
PATCH_FILE_EOF

mkdir -p 'docs'
cat > 'docs/architecture.md' <<'PATCH_FILE_EOF'
# Controller architecture

The controller is split so protocol and safety logic can be tested without energized motors.

1. The TCP service accepts newline-delimited EasyComm commands on port 4553.
2. The EasyComm parser turns commands into bounded azimuth/elevation targets.
3. A motion controller will compare target angles with filtered sensor angles and command each motor.
4. Hardware adapters will own GPIO/PWM, limit switches, and the WT901 serial stream.
5. A safety supervisor will stop both axes on stale sensor data, travel-limit violation, process shutdown, or control timeout.

The current implementation includes steps 1 and 2, simulator mode, the WT901 binary protocol decoder, USB serial transport, calibration utility, live angle feedback in the EasyComm service, status/fault reporting, and stale-feedback protection. GPIO motor output remains disabled until the mounted sensor axis mapping is measured and verified.

## Planned control loop

The motion loop should run independently of TCP clients at a fixed rate. Each axis will use signed angular error, a deadband, ramped PWM, and braking/coasting behavior selected during bench testing. Azimuth is a bounded 0-359 degree axis, not an assumed continuously rotating axis; shortest-path movement must account for cable and hard-stop constraints.

The controller never derives antenna position from motor runtime, belt ratio, or motor revolutions. Hall modules establish the two physical home references; the WT901 supplies all continuous azimuth/elevation feedback. At home, the controller will capture the sensor-to-mechanism offsets and use them until the next homing cycle.

## EasyComm subset

- `AZ123.4 EL45.6` sets both targets.
- `AZ123.4` or `EL45.6` sets one target.
- `AZ EL`, `AZ`, or `EL` queries the current position. The service returns `AZ123.4 EL45.6`.
- `STATUS` returns a non-standard JSON status object for the integrated web UI and diagnostics.
- `SA`, `SE`, `SA SE`, or `STOP` stops motion.
- `ZERO` captures the current feedback position as azimuth 0 and elevation 0.
- `PARK` requests azimuth 0 and elevation 0.

Commands may be terminated by LF or CRLF. Responses use CRLF. The TCP service handles concurrent persistent clients, allowing tracking software and the web control proxy to remain connected at the same time.

The integrated HTTP server listens on port 8080. Its API does not access `RotatorController` directly: every status or control operation opens a TCP connection to `127.0.0.1:4553` and sends the corresponding EasyComm command. This keeps browser testing on the same protocol path used by external tracking software.
PATCH_FILE_EOF

echo 'Patch applied.'
echo 'Recommended validation:'
echo '  ./scripts/test.sh'
echo '  ./scripts/build-rpi.sh'
if [[ "${1:-}" == '--test' ]]; then
  ./scripts/test.sh
fi
