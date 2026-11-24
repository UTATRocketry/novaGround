#include "gpio_controller.hpp"

#include <iostream>

#include "core/output_supervisor.hpp"
#include "core/stats.hpp"
#include "utils/json_utils.hpp"

GpioController::GpioController(DigitalIo* gpio, DataLogger* logger, StatsRegistry* stats)
    : gpio_(gpio), logger_(logger), stats_(stats) {}

void GpioController::set_supervisor(OutputSupervisor* supervisor) {
    std::lock_guard<std::mutex> lock(mutex_);
    supervisor_ = supervisor;
}

bool GpioController::available() const {
    return gpio_ != nullptr;
}

bool GpioController::configured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return configured_;
}

std::vector<int> GpioController::output_pins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> pins;
    for (const auto& [pin, level] : outputs_) pins.push_back(pin);
    return pins;
}

std::vector<int> GpioController::input_pins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {inputs_.begin(), inputs_.end()};
}

std::vector<ConfigRejection> GpioController::apply(const std::set<int>& outputs, const std::set<int>& inputs) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ConfigRejection> failed;
    configured_ = true;

    if (!available()) {
        for (int pin : outputs) failed.push_back({"gpio.outputs." + std::to_string(pin), "no GPIO chip"});
        for (int pin : inputs) failed.push_back({"gpio.inputs." + std::to_string(pin), "no GPIO chip"});
        return failed;
    }

    bool dropped_output = false;
    // Outputs no longer listed: back to the safe level, then released. The
    // release happens even if that write fails: the pin is no longer ours.
    for (auto it = outputs_.begin(); it != outputs_.end();) {
        const int pin = it->first;
        if (outputs.count(pin)) {
            ++it;
            continue;
        }
        if (!gpio_->write(pin, 0)) {
            std::cerr << "GPIO " << pin << ": dropped from the config, but driving it to 0 failed; releasing it anyway"
                      << std::endl;
            if (stats_) stats_->count_gpio_write_failure();
        }
        gpio_->release(pin);
        std::cout << "GPIO " << pin << ": no longer a configured output; driven to 0 and released" << std::endl;
        if (logger_) logger_->update_gpio(pin, 0);
        dropped_output = true;
        it = outputs_.erase(it);
    }
    for (auto it = inputs_.begin(); it != inputs_.end();) {
        if (inputs.count(*it)) {
            ++it;
            continue;
        }
        gpio_->release(*it);
        std::cout << "GPIO " << *it << ": no longer a configured input; released" << std::endl;
        it = inputs_.erase(it);
    }

    // New outputs, claimed at their safe level.
    for (int pin : outputs) {
        if (outputs_.count(pin)) {
            continue;
        }
        if (gpio_->configure_output(pin, 0)) {
            outputs_[pin] = 0;
            if (logger_) logger_->update_gpio(pin, 0);
        } else {
            std::cerr << "GPIO " << pin << ": claim as output failed; it stays unclaimed" << std::endl;
            if (stats_) stats_->count_gpio_claim_failure();
            failed.push_back({"gpio.outputs." + std::to_string(pin), "claim failed (see the Pi's log)"});
        }
    }
    for (int pin : inputs) {
        if (inputs_.count(pin)) {
            continue;
        }
        if (gpio_->configure_input(pin)) {
            inputs_.insert(pin);
        } else {
            std::cerr << "GPIO " << pin << ": claim as input failed; it stays unclaimed" << std::endl;
            if (stats_) stats_->count_gpio_claim_failure();
            failed.push_back({"gpio.inputs." + std::to_string(pin), "claim failed (see the Pi's log)"});
        }
    }

    if (dropped_output && logger_) {
        logger_->log_actuator_snapshot("config");
    }
    return failed;
}

void GpioController::handle_command(const boost::json::object& cmd) {
    auto refuse = [this](const std::string& why) {
        std::cerr << "GPIO command refused: " << why << std::endl;
        if (stats_) stats_->count_refused_command();
    };

    std::lock_guard<std::mutex> lock(mutex_);
    if (!available()) {
        refuse("no GPIO chip on this node");
        return;
    }
    if (cmd.contains("mode")) {
        // Directions come from the config now; a command can't change one.
        refuse("the 'mode' field is no longer accepted (pin directions come from the config)");
        return;
    }
    if (!configured_) {
        refuse("no GPIO config has arrived on the config topic yet");
        return;
    }
    // A whole number only: get_int would also take 17.9 or true.
    const auto* id = cmd.if_contains("id");
    if (!id || !(id->is_int64() || id->is_uint64())) {
        refuse("missing or non-integer id");
        return;
    }
    auto pin_opt = get_int(cmd, "id");
    if (!pin_opt || *pin_opt < 0 || *pin_opt > kMaxBcmPin) {
        refuse("GPIO " + boost::json::serialize(*id) + " is not a configured output");
        return;
    }
    auto state_opt = get_bool(cmd, "state");
    if (!state_opt) {
        refuse("missing state");
        return;
    }
    const int pin = static_cast<int>(*pin_opt);
    auto it = outputs_.find(pin);
    if (it == outputs_.end()) {
        refuse("GPIO " + std::to_string(*pin_opt) + " is not a configured output");
        return;
    }

    const int level = *state_opt ? 1 : 0;
    if (gpio_->write(pin, level)) {
        it->second = level;
        if (supervisor_) supervisor_->command_applied(gpio_output(pin));
    } else {
        std::cerr << "GPIO " << pin << ": write of " << level << " not done; the pin stays at " << it->second
                  << std::endl;
        if (stats_) stats_->count_gpio_write_failure();
    }

    // The level really written, not the one commanded.
    if (logger_) {
        logger_->update_gpio(pin, it->second);
        logger_->log_actuator_snapshot("gpio");
    }
}

void GpioController::drive_safe(int pin, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = outputs_.find(pin);
    if (!available() || it == outputs_.end()) {
        return;
    }
    if (!gpio_->write(pin, 0)) {
        std::cerr << "GPIO " << pin << ": driving it to 0 for a trip (" << reason << ") failed" << std::endl;
        if (stats_) stats_->count_gpio_write_failure();
        return;
    }
    it->second = 0;
    if (supervisor_) supervisor_->record_trip(gpio_output(pin), reason);
    if (logger_) {
        logger_->update_gpio(pin, 0);
        logger_->log_actuator_snapshot("trip");
    }
}
