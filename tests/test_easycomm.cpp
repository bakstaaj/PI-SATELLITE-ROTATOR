#include "rotator/easycomm.hpp"
#include "rotator/motor_backend.hpp"
#include "rotator/rotator.hpp"

#include <chrono>
#include <cmath>
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
    require(parse_easycomm("SENSOR TEST").kind == CommandKind::sensor_test,
            "sensor test command");
    require(parse_easycomm("SENSOR CALIBRATE ACCEL").kind ==
                CommandKind::sensor_calibrate_accel,
            "sensor calibrate accel command");
    require(parse_easycomm("SENSOR CALIBRATE MAGNETIC START").kind ==
                CommandKind::sensor_calibrate_magnetic_start,
            "sensor magnetic start command");
    require(parse_easycomm("SENSOR CALIBRATE MAGNETIC FINISH").kind ==
                CommandKind::sensor_calibrate_magnetic_finish,
            "sensor magnetic finish command");
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


    RotatorController sensor_command_controller;
    require(!sensor_command_controller.request_sensor_action(
                SensorAction::calibrate_accelerometer, error),
            "reject sensor calibration before external feedback is enabled");
    sensor_command_controller.enable_external_feedback();
    require(sensor_command_controller.update_feedback(20.0, 20.0),
            "seed sensor calibration controller");
    require(sensor_command_controller.request_sensor_action(
                SensorAction::calibrate_accelerometer, error),
            "queue sensor calibration request");
    auto pending_action = sensor_command_controller.take_sensor_action();
    require(pending_action && *pending_action == SensorAction::calibrate_accelerometer,
            "take queued sensor calibration request");
    require(!sensor_command_controller.take_sensor_action(),
            "sensor calibration request queue drains");

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
    require(boundary_controller.update_feedback(10.0, 9.5),
            "allow small negative measured elevation below zero");
    require(std::abs(boundary_controller.position().elevation - -0.5) < 0.01,
            "negative measured elevation is reported instead of clamped");
    require(boundary_controller.update_feedback(10.0, 8.5),
            "allow negative measured elevation before limit switch zero");
    require(std::abs(boundary_controller.position().elevation - -1.5) < 0.01,
            "material negative measured elevation is reported");
    require(!boundary_controller.update_feedback(10.0, -81.0),
            "reject implausible negative measured elevation");
    require(boundary_controller.status().fault_reason ==
                "mapped measured elevation outside -90 to 180 degrees",
            "report rejected measured elevation reason");
    require(std::abs(boundary_controller.position().elevation - -1.5) < 0.01,
            "rejected feedback does not update position");

    RotatorController maintenance_controller;
    maintenance_controller.enable_external_feedback();
    maintenance_controller.set_feedback_timeout(10ms);
    require(maintenance_controller.update_feedback(30.0, 30.0),
            "seed maintenance controller");
    maintenance_controller.begin_sensor_maintenance(100ms,
                                                    "test sensor maintenance");
    std::this_thread::sleep_for(25ms);
    const auto maintenance_status = maintenance_controller.status();
    require(maintenance_status.feedback_stale, "maintenance feedback can be stale");
    require(maintenance_status.sensor_maintenance, "sensor maintenance flag is set");
    require(!maintenance_status.fault,
            "sensor maintenance suppresses stale fault");
    require(maintenance_status.sensor_maintenance_reason == "test sensor maintenance",
            "sensor maintenance reason is reported");

    RotatorController stream_controller;
    stream_controller.enable_external_feedback();
    stream_controller.set_feedback_timeout(10ms);
    require(!stream_controller.update_feedback(20.0, 181.5),
            "reject invalid mapped feedback while recording stream frame");
    const auto stream_status = stream_controller.status();
    require(stream_status.sensor_stream_received,
            "sensor stream records invalid mapped frame");
    require(stream_status.sensor_stream_age_ms >= 0,
            "sensor stream age is reported");
    require(!stream_status.feedback_received,
            "invalid mapped frame is not accepted as feedback");

    std::cout << "All tests passed\n";
    return 0;
}
