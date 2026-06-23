#pragma once

#include <string>

namespace rotator {

enum class AxisDirection {
    stopped,
    positive,
    negative,
};

struct MotorCommand {
    AxisDirection azimuth{AxisDirection::stopped};
    AxisDirection elevation{AxisDirection::stopped};

    bool any_motion() const {
        return azimuth != AxisDirection::stopped || elevation != AxisDirection::stopped;
    }
};

class MotorDriver {
public:
    virtual ~MotorDriver() = default;
    virtual std::string name() const = 0;
    virtual void apply(MotorCommand command) = 0;
    virtual void stop() = 0;
};

class NullMotorDriver final : public MotorDriver {
public:
    std::string name() const override;
    void apply(MotorCommand command) override;
    void stop() override;
    MotorCommand last_command() const;

private:
    MotorCommand last_command_;
};

}  // namespace rotator
