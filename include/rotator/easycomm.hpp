#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rotator {

enum class CommandKind {
    set_position,
    query_position,
    status,
    sensor_test,
    sensor_calibrate_accel,
    sensor_calibrate_magnetic_start,
    sensor_calibrate_magnetic_finish,
    stop,
    zero,
    park,
    invalid
};

struct Command {
    CommandKind kind{CommandKind::invalid};
    std::optional<double> azimuth;
    std::optional<double> elevation;
    std::string error;
};

Command parse_easycomm(std::string_view line);
std::string format_position(double azimuth, double elevation);

}  // namespace rotator
