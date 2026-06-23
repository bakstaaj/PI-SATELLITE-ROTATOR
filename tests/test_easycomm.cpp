#include "rotator/easycomm.hpp"
#include "rotator/motor_backend.hpp"
#include "rotator/rotator.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class RecordingMotorDriver final : public rotator::MotorDriver {
public:
    std::string name() const override { return "recording"; }
    void apply(rotator::MotorCommand command) override { last = command; ++apply_count; }
    void stop() override { last = {}; ++stop_count; }
    rotator::MotorCommand last;
    int apply_count{0};
    int stop_count{0};
};
}

int main() {
    using namespace rotator;
    using namespace std::chrono_literals;

    const auto set = parse_easycomm("AZ123.4 EL45.6\r\n");
    require(set.kind == CommandKind::set_position, "set command kind");
    require(set.azimuth == 123.4 && set.elevation == 45.6, "set values");

    require(parse_easycomm("AZ EL").kind == CommandKind::query_position, "position query");
    require(parse_easycomm("STATUS").kind == CommandKind::status, "status command");
    require(parse_easycomm("SA SE").kind == CommandKind::stop, "stop command");
    require(parse_easycomm("ZERO").kind == CommandKind::zero, "zero command");
    require(parse_easycomm("PARK").kind == CommandKind::park, "park command");
    require(parse_easycomm("AZnope").kind == CommandKind::invalid, "reject malformed value");
    require(parse_easycomm("AZ1 AZ2").error == "duplicate axis token",
            "identify duplicate axis token");

    RotatorController controller;
    std::string error;
    require(controller.set_target(359.0, 180.0, error), "accept limits");
    require(!controller.set_target(359.1, std::nullopt, error), "reject azimuth above limit");
    require(!controller.set_target(std::nullopt, -0.1, error), "reject negative elevation");
    require(format_position(12.34, 5.67) == "AZ12.3 EL5.7\r\n", "format response");

    const auto simulator_status = controller.status();
    require(!simulator_status.external_feedback, "simulator reports no external feedback");
    require(simulator_status.target_azimuth == 359.0, "status reports target azimuth");
    require(simulator_status.motor_backend == "simulator", "status reports simulator motor backend");

    controller.enable_external_feedback();
    require(!controller.set_target(125.0, 35.0, error),
            "reject external motion before feedback");
    require(controller.update_feedback(120.0, 35.0), "accept external feedback");
    require(controller.set_target(125.0, 35.0, error), "set external target");
    require(controller.position().moving, "external target reports motion needed");
    controller.stop();
    require(!controller.position().moving, "stop clears motion state");
    require(!controller.update_feedback(120.0, 181.0), "reject invalid feedback");
    require(controller.zero_current_position(), "zero stopped controller");
    require(controller.position().azimuth == 0.0 && controller.position().elevation == 0.0,
            "zero current position");
    require(controller.update_feedback(130.0, 40.0), "feedback after zero");
    require(controller.position().azimuth == 10.0 && controller.position().elevation == 5.0,
            "zero offsets external feedback");
    require(controller.set_target(140.0, 45.0, error), "set moving target before zero test");
    require(!controller.zero_current_position(), "reject zero while moving");
    controller.stop();
    require(controller.zero_current_position(), "allow zero after stop");

    RotatorController stale_controller;
    stale_controller.enable_external_feedback();
    stale_controller.set_feedback_timeout(10ms);
    require(stale_controller.update_feedback(20.0, 20.0), "seed stale feedback test");
    require(!stale_controller.status().feedback_stale, "fresh feedback is not stale");
    std::this_thread::sleep_for(25ms);
    require(stale_controller.status().feedback_stale, "feedback becomes stale");
    require(stale_controller.status().fault, "stale feedback reports fault");
    require(!stale_controller.set_target(25.0, 25.0, error),
            "reject target when feedback is stale");

    RotatorController boundary_controller;
    boundary_controller.enable_external_feedback();
    require(!boundary_controller.zero_current_position(), "reject zero before first feedback");
    require(boundary_controller.update_feedback(10.0, 10.0), "seed boundary feedback");
    require(boundary_controller.zero_current_position(), "zero boundary controller");
    require(boundary_controller.update_feedback(10.0, 9.5), "tolerate elevation noise at zero");
    require(boundary_controller.position().elevation == 0.0, "clamp boundary noise to zero");
    require(!boundary_controller.update_feedback(10.0, 8.5), "reject material boundary error");
    require(boundary_controller.status().fault_reason == "mapped elevation outside 0-180 degrees",
            "report rejected feedback reason");
    require(boundary_controller.position().elevation == 0.0,
            "rejected feedback does not update position");

    std::cout << "All tests passed\n";
    return 0;
}
