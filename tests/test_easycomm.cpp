#include "rotator/easycomm.hpp"
#include "rotator/rotator.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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
    const auto set = parse_easycomm("AZ123.4 EL45.6\r\n");
    require(set.kind == CommandKind::set_position, "set command kind");
    require(set.azimuth == 123.4 && set.elevation == 45.6, "set values");

    require(parse_easycomm("AZ EL").kind == CommandKind::query_position, "position query");
    require(parse_easycomm("SA SE").kind == CommandKind::stop, "stop command");
    require(parse_easycomm("ZERO").kind == CommandKind::zero, "zero command");
    require(parse_easycomm("PARK").kind == CommandKind::park, "park command");
    require(parse_easycomm("AZnope").kind == CommandKind::invalid, "reject malformed value");

    RotatorController controller;
    std::string error;
    require(controller.set_target(359.0, 180.0, error), "accept limits");
    require(!controller.set_target(359.1, std::nullopt, error), "reject azimuth above limit");
    require(!controller.set_target(std::nullopt, -0.1, error), "reject negative elevation");
    require(format_position(12.34, 5.67) == "AZ12.3 EL5.7\r\n", "format response");

    controller.enable_external_feedback();
    require(controller.update_feedback(120.0, 35.0), "accept external feedback");
    require(controller.set_target(125.0, 35.0, error), "set external target");
    require(controller.position().moving, "external target reports motion needed");
    controller.stop();
    require(!controller.position().moving, "stop clears motion state");
    require(!controller.update_feedback(120.0, 181.0), "reject invalid feedback");
    controller.zero_current_position();
    require(controller.position().azimuth == 0.0 && controller.position().elevation == 0.0,
            "zero current position");
    require(controller.update_feedback(130.0, 40.0), "feedback after zero");
    require(controller.position().azimuth == 10.0 && controller.position().elevation == 5.0,
            "zero offsets external feedback");

    std::cout << "All tests passed\n";
    return 0;
}
