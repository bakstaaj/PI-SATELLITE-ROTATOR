#include "rotator/gpio_motor_backend.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace rotator {
namespace {

constexpr const char* kGpioSysfsRoot = "/sys/class/gpio";

std::filesystem::path gpio_root() { return kGpioSysfsRoot; }

std::filesystem::path gpio_path(int gpio) {
    return gpio_root() / ("gpio" + std::to_string(gpio));
}

void validate_pin(int gpio) {
    if (gpio < 0) {
        throw std::invalid_argument("GPIO pin numbers must be non-negative");
    }
}

void write_text(const std::filesystem::path& path, const std::string& value) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("unable to open " + path.string());
    }
    out << value;
    if (!out) {
        throw std::runtime_error("unable to write " + path.string());
    }
}

void export_pin(int gpio) {
    validate_pin(gpio);
    const auto path = gpio_path(gpio);
    if (!std::filesystem::exists(path)) {
        write_text(gpio_root() / "export", std::to_string(gpio));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("GPIO " + std::to_string(gpio) + " did not appear after export");
    }
    write_text(path / "direction", "out");
    write_text(path / "value", "0");
}

void set_pin(int gpio, bool value) {
    write_text(gpio_path(gpio) / "value", value ? "1" : "0");
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

std::string GpioMotorDriver::name() const { return "gpio-sysfs"; }

void GpioMotorDriver::configure_axis(const AxisGpioPins& pins) {
    const std::array<int, 3> values{pins.enable, pins.forward, pins.reverse};
    for (const int gpio : values) {
        export_pin(gpio);
    }
}

void GpioMotorDriver::apply_axis(const AxisGpioPins& pins, AxisDirection direction) {
    direction = maybe_invert(direction, pins.invert);

    if (direction == AxisDirection::stopped) {
        set_pin(pins.enable, false);
        set_pin(pins.forward, false);
        set_pin(pins.reverse, false);
        return;
    }

    if (direction == AxisDirection::positive) {
        set_pin(pins.reverse, false);
        set_pin(pins.forward, true);
        set_pin(pins.enable, true);
        return;
    }

    set_pin(pins.forward, false);
    set_pin(pins.reverse, true);
    set_pin(pins.enable, true);
}

void GpioMotorDriver::apply(MotorCommand command) {
    apply_axis(pins_.azimuth, command.azimuth);
    apply_axis(pins_.elevation, command.elevation);
}

void GpioMotorDriver::stop() { apply(MotorCommand{}); }

}  // namespace rotator
