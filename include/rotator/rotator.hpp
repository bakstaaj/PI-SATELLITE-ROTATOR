#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace rotator {

struct Position {
    double azimuth{0.0};
    double elevation{0.0};
    bool moving{false};
};

class RotatorController {
public:
    bool set_target(std::optional<double> azimuth,
                    std::optional<double> elevation,
                    std::string& error);
    Position position() const;
    void stop();
    bool zero_current_position();
    void enable_external_feedback();
    bool update_feedback(double azimuth, double elevation);

private:
    void update_motion_state_locked();

    mutable std::mutex mutex_;
    Position state_;
    Position target_;
    Position raw_feedback_;
    Position feedback_zero_;
    bool external_feedback_{false};
    bool feedback_received_{false};
    bool motion_commanded_{false};
};

}  // namespace rotator
