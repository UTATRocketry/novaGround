#include "loops.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace json = boost::json;
using namespace std::chrono;

namespace {
const std::string kTelemetryTopic = "novaground/telemetry";
const std::string kUartTopic = "novaground/uart";

uint32_t get_u32_le(const std::array<uint8_t, kUartFrameMaxPayload>& data, size_t off) {
    return static_cast<uint32_t>(data[off]) |
           (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

float get_f32_le(const std::array<uint8_t, kUartFrameMaxPayload>& data, size_t off) {
    uint32_t raw = get_u32_le(data, off);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

double get_f64_le(const std::array<uint8_t, kUartFrameMaxPayload>& data, size_t off) {
    uint64_t raw = 0;
    for (size_t i = 0; i < 8; ++i) {
        raw |= static_cast<uint64_t>(data[off + i]) << (8 * i);
    }
    double value = 0.0;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

json::array uart_payload_to_json(const UartFrame& frame) {
    json::array payload;
    for (uint8_t i = 0; i < frame.len; ++i) {
        payload.push_back(static_cast<int>(frame.payload[i]));
    }
    return payload;
}

json::object vector3_to_json(const UartFrame& frame, size_t off) {
    json::object obj;
    obj["x"] = get_f32_le(frame.payload, off);
    obj["y"] = get_f32_le(frame.payload, off + 4);
    obj["z"] = get_f32_le(frame.payload, off + 8);
    return obj;
}

json::object fmc_telem_to_json(const UartFrame& frame) {
    json::object obj;
    if (frame.len < 111) {
        obj["decode_error"] = "short_fmc_telem";
        return obj;
    }

    size_t off = 0;
    obj["timestamp_ms"] = get_u32_le(frame.payload, off);
    off += 4;

    obj["accel"] = vector3_to_json(frame, off);
    off += 12;
    obj["accel_valid"] = static_cast<bool>(frame.payload[off++]);

    obj["imu_accel"] = vector3_to_json(frame, off);
    off += 12;
    obj["imu_gyro"] = vector3_to_json(frame, off);
    off += 12;
    obj["imu_valid"] = static_cast<bool>(frame.payload[off++]);

    obj["mag"] = vector3_to_json(frame, off);
    off += 12;
    obj["mag_valid"] = static_cast<bool>(frame.payload[off++]);

    obj["baro_temp"] = get_f32_le(frame.payload, off);
    off += 4;
    obj["baro_pressure"] = get_f32_le(frame.payload, off);
    off += 4;
    obj["baro_altitude"] = get_f32_le(frame.payload, off);
    off += 4;
    obj["baro_valid"] = static_cast<bool>(frame.payload[off++]);

    obj["gps_latitude"] = get_f64_le(frame.payload, off);
    off += 8;
    obj["gps_longitude"] = get_f64_le(frame.payload, off);
    off += 8;
    obj["gps_altitude"] = get_f32_le(frame.payload, off);
    off += 4;
    obj["gps_speed"] = get_f32_le(frame.payload, off);
    off += 4;
    obj["gps_course"] = get_f32_le(frame.payload, off);
    off += 4;
    obj["gps_sats"] = static_cast<int>(frame.payload[off++]);
    obj["gps_fix"] = static_cast<int>(frame.payload[off++]);
    obj["gps_valid"] = static_cast<bool>(frame.payload[off++]);
    obj["gps_hour"] = static_cast<int>(frame.payload[off++]);
    obj["gps_minute"] = static_cast<int>(frame.payload[off++]);
    obj["gps_second"] = static_cast<int>(frame.payload[off++]);

    obj["temp_h7"] = get_f32_le(frame.payload, off);
    off += 4;
    obj["temp_pwr"] = get_f32_le(frame.payload, off);
    off += 4;
    obj["temp_valid"] = static_cast<bool>(frame.payload[off++]);
    return obj;
}

json::object uart_ack_to_json(const UartFrame& frame) {
    json::object obj;
    if (frame.len == 1) {
        obj["ack_schema"] = "status_only";
        obj["status"] = static_cast<int>(frame.payload[0]);
        return obj;
    }

    if (frame.len >= 9) {
        obj["ack_schema"] = "can_bridge";
        obj["sender"] = static_cast<int>(frame.payload[0]);
        obj["cmd_id"] = get_u32_le(frame.payload, 1);
        obj["opcode"] = static_cast<int>(
            static_cast<uint16_t>(frame.payload[5]) |
            (static_cast<uint16_t>(frame.payload[6]) << 8));
        obj["ok"] = static_cast<bool>(frame.payload[7]);
        obj["status"] = static_cast<int>(frame.payload[8]);
        return obj;
    }

    obj["ack_schema"] = "unknown";
    return obj;
}

json::object uart_frame_to_json(const UartFrame& frame) {
    json::object obj;
    obj["ver"] = static_cast<int>(frame.ver);
    obj["msg"] = static_cast<int>(frame.msg);
    obj["msg_name"] = uart_msg_type_name(frame.msg);
    obj["len"] = static_cast<int>(frame.len);
    obj["seq"] = static_cast<int>(frame.seq);
    obj["flags"] = static_cast<int>(frame.flags);
    obj["src"] = static_cast<int>(frame.src);
    obj["payload"] = uart_payload_to_json(frame);

    if (frame.msg == UART_MSG_ACK) {
        obj["decoded"] = uart_ack_to_json(frame);
    } else if (frame.msg == UART_MSG_ERR && frame.len >= 1) {
        obj["error"] = static_cast<int>(frame.payload[0]);
    } else if (frame.msg == UART_MSG_EVENT && frame.len >= 1) {
        obj["event"] = static_cast<int>(frame.payload[0]);
    } else if (frame.msg == UART_MSG_TELEM && frame.src == UART_NODE_FMC) {
        obj["telem_schema"] = "fmc_snapshot_v1";
        obj["decoded"] = fmc_telem_to_json(frame);
    } else if (frame.msg == UART_MSG_TELEM && frame.len >= 3) {
        obj["telem_schema"] = "can_bridge";
        obj["sender"] = static_cast<int>(frame.payload[0]);
        obj["target"] = static_cast<int>(frame.payload[1]);
        obj["telem_len"] = static_cast<int>(frame.payload[2]);
    }

    return obj;
}
}

void publisher_loop(mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms) {
    int interval_ms = publish_interval_ms > 0 ? publish_interval_ms : 1;
    while (true) {
        try {
            json::array json_sensor_data;
            json::array json_gpio_data;

            for (const auto& sd : telemetry.snapshot_sensors()) {
                json::object se;
                se["hat_id"] = sd.hat_id;
                se["channel_id"] = sd.channel_id;
                se["value"] = sd.value;
                se["timestamp"] = sd.timestamp_ms;
                json_sensor_data.push_back(se);
            }

            for (const auto& [pin, state] : telemetry.snapshot_gpio_states()) {
                json::object g;
                g["pin_id"] = pin;
                g["state"] = state;
                json_gpio_data.push_back(g);
            }

            json::object payload;
            payload["source"] = source_id;
            payload["sensors"] = json_sensor_data;
            payload["gpios"] = json_gpio_data;

            std::string s_payload = json::serialize(payload);

            if (cli && cli->is_connected()) {
                try {
                    cli->publish(kTelemetryTopic, s_payload)->wait();
                } catch (const std::exception& e) {
                    std::cerr << "Publish failed: " << e.what() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Publisher error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Publisher error: unknown exception" << std::endl;
        }

        std::this_thread::sleep_for(milliseconds(interval_ms));
    }
}

void consumer_loop(mqtt::async_client_ptr cli, CommandRouter& router) {
    while (true) {
        try {
            auto msg = cli ? cli->consume_message() : nullptr;
            if (!msg) {
                std::this_thread::sleep_for(milliseconds(1));
                continue;
            }

            std::string m_str = msg->to_string();
            router.handle_message(m_str);
        } catch (const std::exception& e) {
            std::cerr << "Consumer error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Consumer error: unknown exception" << std::endl;
        }
    }
}

void gpio_sampler_loop(GPIO_Manager& manager, TelemetryStore& telemetry) {
    while (true) {
        try {
            auto input_vals = manager.read_all_inputs();
            telemetry.set_gpio_states(std::move(input_vals));
        } catch (const std::exception& e) {
            std::cerr << "GPIO sampler error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "GPIO sampler error: unknown exception" << std::endl;
        }
        std::this_thread::sleep_for(milliseconds(50));
    }
}

void uart_rx_loop(UartLink& uart, mqtt::async_client_ptr cli, std::string source_id) {
    while (true) {
        try {
            auto frame = uart.read_frame(milliseconds(100));
            if (!frame) {
                continue;
            }

            json::object payload;
            payload["source"] = source_id;
            payload["frame"] = uart_frame_to_json(*frame);

            if (cli && cli->is_connected()) {
                try {
                    cli->publish(kUartTopic, json::serialize(payload))->wait();
                } catch (const std::exception& e) {
                    std::cerr << "UART publish failed: " << e.what() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "UART RX error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "UART RX error: unknown exception" << std::endl;
        }
    }
}
