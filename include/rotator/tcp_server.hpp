#pragma once

#include "rotator/rotator.hpp"

#include <atomic>
#include <cstdint>

namespace rotator {

class TcpServer {
public:
    TcpServer(std::uint16_t port, RotatorController& controller);
    int run(std::atomic_bool& stopping);

private:
    std::uint16_t port_;
    RotatorController& controller_;
};

}  // namespace rotator
