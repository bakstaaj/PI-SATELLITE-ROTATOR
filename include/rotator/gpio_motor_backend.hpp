#pragma once

#include "rotator/motor_backend.hpp"

#include <string>

namespace rotator {

struct AxisGpioPins {
    int enable{-1};
    int forward{-1};
    int reverse{-1};
    bool invert{false};
};

struct GpioMotorPins {
    AxisGpioPins azimuth{.enable = 12, .forward = 5, .reverse = 6, .invert = false};
    AxisGpioPins elevation{.enable = 13, .forward = 16, .reverse = 20, .invert = false};
};

class GpioMotorDriver final : public MotorDriver {
public:
    explicit GpioMotorDriver(GpioMotorPins pins);
    ~GpioMotorDriver() override;

    std::string name() const override;
    void apply(MotorCommand command) override;
    void stop() override;

private:
    void configure_axis(const AxisGpioPins& pins);
    void apply_axis(const AxisGpioPins& pins, AxisDirection direction);

    GpioMotorPins pins_;
};

}  // namespace rotator
