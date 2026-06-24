#include "rotator/web_server.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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



std::optional<std::string> read_text_asset(std::string_view asset_name) {
    if (asset_name.empty() || asset_name.find("..") != std::string_view::npos ||
        asset_name.find('\\') != std::string_view::npos || asset_name.front() == '/') {
        return std::nullopt;
    }
    const std::array<std::filesystem::path, 4> roots{
        std::filesystem::path{"/opt/pi-satellite-rotator/web"},
        std::filesystem::path{"/src/web"},
        std::filesystem::path{"web"},
        std::filesystem::path{"../web"},
    };
    for (const auto& root : roots) {
        std::ifstream input(root / std::string(asset_name), std::ios::binary);
        if (!input) { continue; }
        std::ostringstream body;
        body << input.rdbuf();
        return body.str();
    }
    return std::nullopt;
}

std::string_view web_content_type(std::string_view asset_name) {
    if (asset_name.ends_with(".css")) { return "text/css; charset=utf-8"; }
    if (asset_name.ends_with(".js")) { return "application/javascript; charset=utf-8"; }
    return "text/html; charset=utf-8";
}

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
             << "Content-Security-Policy: default-src 'self'; style-src 'self'; script-src 'self'\r\n"
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


bool serve_web_asset(int client, std::string_view target) {
    std::string_view asset_name;
    if (target == "/" || target == "/index.html") {
        asset_name = "index.html";
    } else if (target == "/static/app.css") {
        asset_name = "app.css";
    } else if (target == "/static/app.js") {
        asset_name = "app.js";
    } else {
        return false;
    }
    if (const auto body = read_text_asset(asset_name)) {
        http_response(client, 200, web_content_type(asset_name), *body);
    } else {
        http_response(client, 404, "application/json", "{\"ok\":false,\"error\":\"web asset not installed\"}");
    }
    return true;
}

void handle_request(int client, const HttpRequest& request, const std::string& host,
                    std::uint16_t port) {
    try {
        if (request.method == "GET" && serve_web_asset(client, request.target)) {
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
        if (request.method == "POST" && request.target == "/api/sensor/test") {
            const auto raw = connect_easycomm(host, port, "SENSOR TEST\n", true);
            if (!raw.starts_with("{")) {
                throw std::runtime_error("invalid EasyComm sensor test response");
            }
            http_response(client, 200, "application/json; charset=utf-8", raw);
            return;
        }
        if (request.method == "POST" && request.target == "/api/sensor/calibrate-accel") {
            require_easycomm_ok(connect_easycomm(host, port, "SENSOR CALIBRATE ACCEL\n", true));
            http_response(client, 200, "application/json", "{\"ok\":true}");
            return;
        }
        if (request.method == "POST" && request.target == "/api/sensor/calibrate-magnetic-start") {
            require_easycomm_ok(connect_easycomm(host, port, "SENSOR CALIBRATE MAGNETIC START\n", true));
            http_response(client, 200, "application/json", "{\"ok\":true}");
            return;
        }
        if (request.method == "POST" && request.target == "/api/sensor/calibrate-magnetic-finish") {
            require_easycomm_ok(connect_easycomm(host, port, "SENSOR CALIBRATE MAGNETIC FINISH\n", true));
            http_response(client, 200, "application/json", "{\"ok\":true}");
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
        if (request.method == "POST" && request.target == "/api/zero/az") {
            require_easycomm_ok(connect_easycomm(host, port, "ZERO AZ\n", true));
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
