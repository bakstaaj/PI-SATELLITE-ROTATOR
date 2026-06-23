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

    if (text == "SENSOR TEST") {
        return {CommandKind::sensor_test, std::nullopt, std::nullopt, {}};
    }
    if (text == "SENSOR CALIBRATE ACCEL" || text == "SENSOR CAL ACCEL" ||
        text == "SENSOR CALIBRATE ACCELEROMETER") {
        return {CommandKind::sensor_calibrate_accel, std::nullopt, std::nullopt, {}};
    }
    if (text == "SENSOR CALIBRATE MAGNETIC START" || text == "SENSOR MAG START") {
        return {CommandKind::sensor_calibrate_magnetic_start, std::nullopt, std::nullopt, {}};
    }
    if (text == "SENSOR CALIBRATE MAGNETIC FINISH" || text == "SENSOR MAG FINISH" ||
        text == "SENSOR CALIBRATE MAGNETIC SAVE" || text == "SENSOR MAG SAVE") {
        return {CommandKind::sensor_calibrate_magnetic_finish, std::nullopt, std::nullopt, {}};
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
