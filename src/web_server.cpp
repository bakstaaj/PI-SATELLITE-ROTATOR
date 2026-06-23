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
