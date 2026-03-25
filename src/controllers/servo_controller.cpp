#include "servo_controller.hpp"

#include <iostream>

#include "utils/json_utils.hpp"

namespace {
const int kServoDefaultMicros = 1500;
}

ServoController::ServoController(Adafruit_PWMServoDriver* driver, TelemetryStore* telemetry, DataLogger* logger)
    : driver_(driver), telemetry_(telemetry), logger_(logger) {}

bool ServoController::available() const {
    return driver_ && driver_->is_ready();
}

void ServoController::handle_command(const boost::json::object& cmd) {
    if (!available()) {
        std::cerr << "Servo controller unavailable." << std::endl;
        return;
    }

    auto id_opt = get_int(cmd, "id");
    if (!id_opt) {
        std::cerr << "Servo command missing id." << std::endl;
        return;
    }

    int id = static_cast<int>(*id_opt);
    if (id < 0 || id > 15) {
        std::cerr << "Servo id out of range: " << id << std::endl;
        return;
    }

    auto angle_it = cmd.if_contains("angle");
    if (!angle_it) {
        std::cerr << "Servo command missing angle." << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ServoState& state = states_[id];

    if (angle_it->is_string()) {
        std::string action = to_lower(std::string(angle_it->as_string().c_str()));
        if (action == "off") {
            state.enabled = false;
            driver_->disablePWM(static_cast<uint8_t>(id));
            if (telemetry_) {
                telemetry_->upsert_servo_state(id, state.last_angle, false);
            }
            if (logger_) {
                logger_->update_servo(id, state.last_angle, false);
                logger_->log_actuator_snapshot("servo");
            }
            return;
        }
        if (action == "on") {
            if (!state.has_angle) {
                state.last_angle = kServoDefaultMicros;
                state.has_angle = true;
            }
            state.enabled = true;
            driver_->writeMicroseconds(static_cast<uint8_t>(id), state.last_angle);
            if (telemetry_) {
                telemetry_->upsert_servo_state(id, state.last_angle, true);
            }
            if (logger_) {
                logger_->update_servo(id, state.last_angle, true);
                logger_->log_actuator_snapshot("servo");
            }
            return;
        }
        std::cerr << "Unknown servo angle command: " << action << std::endl;
        return;
    }

    auto angle_opt = get_int(cmd, "angle");
    if (!angle_opt) {
        std::cerr << "Servo angle not numeric." << std::endl;
        return;
    }

    uint16_t angle = static_cast<uint16_t>(*angle_opt);
    state.last_angle = angle;
    state.has_angle = true;
    state.enabled = true;
    driver_->writeMicroseconds(static_cast<uint8_t>(id), angle);
    if (telemetry_) {
        telemetry_->upsert_servo_state(id, angle, true);
    }
    if (logger_) {
        logger_->update_servo(id, angle, true);
        logger_->log_actuator_snapshot("servo");
    }
}

