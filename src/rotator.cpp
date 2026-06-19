#include "rotator/rotator.hpp"

#include <cmath>

namespace rotator {
namespace {
double azimuth_error(double target, double actual) {
    double error = target - actual;
    while (error > 180.0) {
        error -= 360.0;
    }
    while (error < -180.0) {
        error += 360.0;
    }
    return error;
}

double normalize_azimuth(double value) {
    while (value >= 360.0) {
        value -= 360.0;
    }
    while (value < 0.0) {
        value += 360.0;
    }
    return value;
}
}

bool RotatorController::set_target(std::optional<double> azimuth,
                                   std::optional<double> elevation,
                                   std::string& error) {
    if (azimuth && (*azimuth < 0.0 || *azimuth > 359.0)) {
        error = "azimuth must be between 0 and 359 degrees";
        return false;
    }
    if (elevation && (*elevation < 0.0 || *elevation > 180.0)) {
        error = "elevation must be between 0 and 180 degrees";
        return false;
    }

    std::lock_guard lock(mutex_);
    if (azimuth) {
        target_.azimuth = *azimuth;
    }
    if (elevation) {
        target_.elevation = *elevation;
    }
    motion_commanded_ = true;
    if (!external_feedback_) {
        state_.azimuth = target_.azimuth;
        state_.elevation = target_.elevation;
        motion_commanded_ = false;
    }
    update_motion_state_locked();
    return true;
}

Position RotatorController::position() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void RotatorController::stop() {
    std::lock_guard lock(mutex_);
    motion_commanded_ = false;
    state_.moving = false;
}

void RotatorController::zero_current_position() {
    std::lock_guard lock(mutex_);
    motion_commanded_ = false;
    if (external_feedback_) {
        feedback_zero_.azimuth = raw_feedback_.azimuth;
        feedback_zero_.elevation = raw_feedback_.elevation;
    }
    state_.azimuth = 0.0;
    state_.elevation = 0.0;
    state_.moving = false;
    target_ = state_;
}

void RotatorController::enable_external_feedback() {
    std::lock_guard lock(mutex_);
    external_feedback_ = true;
    motion_commanded_ = false;
    state_.moving = false;
}

bool RotatorController::update_feedback(double azimuth, double elevation) {
    if (azimuth < 0.0 || azimuth >= 360.0 || elevation < 0.0 || elevation > 180.0) {
        return false;
    }
    std::lock_guard lock(mutex_);
    raw_feedback_.azimuth = azimuth;
    raw_feedback_.elevation = elevation;
    const double mapped_azimuth = normalize_azimuth(azimuth - feedback_zero_.azimuth);
    const double mapped_elevation = elevation - feedback_zero_.elevation;
    if (mapped_elevation < 0.0 || mapped_elevation > 180.0) {
        return false;
    }
    state_.azimuth = mapped_azimuth;
    state_.elevation = mapped_elevation;
    update_motion_state_locked();
    return true;
}

void RotatorController::update_motion_state_locked() {
    constexpr double deadband = 0.5;
    state_.moving = external_feedback_ && motion_commanded_ &&
                    (std::abs(azimuth_error(target_.azimuth, state_.azimuth)) > deadband ||
                     std::abs(target_.elevation - state_.elevation) > deadband);
}

}  // namespace rotator
