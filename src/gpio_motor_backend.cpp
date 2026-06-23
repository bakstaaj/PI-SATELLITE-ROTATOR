#include "rotator/gpio_motor_backend.hpp"

#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sys/wait.h>

namespace rotator {
namespace {

constexpr const char* kPinctrlCommand = "pinctrl";

void validate_pin(int gpio) {
    if (gpio < 0) {
        throw std::invalid_argument("GPIO pin numbers must be non-negative");
    }
}

std::string pin_mode(bool value) { return value ? "dh" : "dl"; }

void run_pinctrl(int gpio, bool value) {
    validate_pin(gpio);

    const std::string command = std::string(kPinctrlCommand) + " set " +
                                std::to_string(gpio) + " op " + pin_mode(value);
    const int result = std::system(command.c_str());
    if (result == -1) {
        throw std::runtime_error("failed to execute pinctrl");
    }
    if (!WIFEXITED(result) || WEXITSTATUS(result) != 0) {
        throw std::runtime_error("pinctrl command failed for GPIO " +
                                 std::to_string(gpio));
    }
}

AxisDirection maybe_invert(AxisDirection direction, bool invert) {
    if (!invert) {
        return direction;
    }
    if (direction == AxisDirection::positive) {
        return AxisDirection::negative;
    }
    if (direction == AxisDirection::negative) {
        return AxisDirection::positive;
    }
    return direction;
}

}  // namespace

GpioMotorDriver::GpioMotorDriver(GpioMotorPins pins) : pins_(pins) {
    configure_axis(pins_.azimuth);
    configure_axis(pins_.elevation);
    stop();
}

GpioMotorDriver::~GpioMotorDriver() {
    try {
        stop();
    } catch (...) {
    }
}

std::string GpioMotorDriver::name() const { return "gpio-pinctrl"; }

void GpioMotorDriver::configure_axis(const AxisGpioPins& pins) {
    const std::array<int, 3> values{pins.enable, pins.forward, pins.reverse};
    for (const int gpio : values) {
        validate_pin(gpio);
    }
    apply_axis(pins, AxisDirection::stopped);
}

void GpioMotorDriver::apply_axis(const AxisGpioPins& pins, AxisDirection direction) {
    direction = maybe_invert(direction, pins.invert);

    if (direction == AxisDirection::stopped) {
        run_pinctrl(pins.enable, false);
        run_pinctrl(pins.forward, false);
        run_pinctrl(pins.reverse, false);
        return;
    }

    if (direction == AxisDirection::positive) {
        run_pinctrl(pins.enable, false);
        run_pinctrl(pins.reverse, false);
        run_pinctrl(pins.forward, true);
        run_pinctrl(pins.enable, true);
        return;
    }

    run_pinctrl(pins.enable, false);
    run_pinctrl(pins.forward, false);
    run_pinctrl(pins.reverse, true);
    run_pinctrl(pins.enable, true);
}

void GpioMotorDriver::apply(MotorCommand command) {
    apply_axis(pins_.azimuth, command.azimuth);
    apply_axis(pins_.elevation, command.elevation);
}

void GpioMotorDriver::stop() { apply(MotorCommand{}); }

}  // namespace rotator
