#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace rotator {

class WebServer {
public:
    WebServer(std::uint16_t http_port, std::string easycomm_host,
              std::uint16_t easycomm_port);
    int run(std::atomic_bool& stopping);

private:
    std::uint16_t http_port_;
    std::string easycomm_host_;
    std::uint16_t easycomm_port_;
};

}  // namespace rotator
