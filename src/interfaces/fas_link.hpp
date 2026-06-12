#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "fas_proto/rt_proto.h"
#include "fas_serial.hpp"

// Protocol layer on top of FasSerial.
//
// Decodes incoming CAN IDs and payloads, routes them to typed callbacks,
// and provides typed send helpers for every outbound command type.
//
// Usage:
//   FasSerial serial("/dev/ttyUSB0", 460800);
//   FasLink   link(serial);
//   link.on_adc_sample([](int bid, const rt_adc_sample_t& s){ ... });
//   serial.set_frame_callback([&link](uint32_t id, const uint8_t* d, size_t l){
//       link.handle_frame(id, d, l);
//   });
//   serial.open();
//   // serial.run() on a thread, link.discovery_tick() periodically

class FasLink {
public:
    // ADC scaling constants (mirror of gs/server.py).
    // Firmware shifts 24-bit code right by 8 → int16. Recover with ×256.
    // ADS131B04 differential FS = ±1.2 V at gain 1; code range = ±2^23.
    static constexpr double kAdcInt16ToV  = 256.0 * 1.2 / (1 << 23);
    static constexpr double kAdcInt16ToMA = kAdcInt16ToV * 1000.0 / 100.0; // 100 Ω shunt

    explicit FasLink(FasSerial& serial);

    // ---- Inbound callbacks ------------------------------------------------
    // All callbacks fire on the serial read thread — keep them short or hand
    // data off to TelemetryStore under a lock.

    void on_adc_sample(std::function<void(int board_id, const rt_adc_sample_t&)> cb);
    void on_board_status(std::function<void(int board_id, const rt_board_status_t&)> cb);
    void on_imc_status(std::function<void(int board_id, const rt_imc_status_t&)> cb);
    void on_announce(std::function<void(const rt_announce_t&)> cb);
    void on_heartbeat(std::function<void(int board_id, const rt_heartbeat_t&)> cb);

    // FMC onboard sensors
    void on_fmc_imu_accel(std::function<void(const rt_fmc_vec3_t&)> cb);
    void on_fmc_imu_gyro(std::function<void(const rt_fmc_vec3_t&)> cb);
    void on_fmc_accel_hg(std::function<void(const rt_fmc_vec3_t&)> cb);
    void on_fmc_mag(std::function<void(const rt_fmc_vec3_t&)> cb);
    void on_fmc_baro(std::function<void(const rt_fmc_baro_t&)> cb);
    void on_fmc_gps_pos(std::function<void(const rt_fmc_gps_pos_t&)> cb);
    void on_fmc_gps_info(std::function<void(const rt_fmc_gps_info_t&)> cb);
    void on_fmc_health(std::function<void(const rt_fmc_health_t&)> cb);

    // PMB
    void on_pmb_pwr(std::function<void(const rt_pmb_pwr_t&)> cb);
    void on_pmb_vmon(std::function<void(const rt_pmb_vmon_t&)> cb);
    void on_pmb_temp(std::function<void(const rt_pmb_temp_t&)> cb);

    // Fires for every decoded frame before typed dispatch.
    // Use for raw console output or protocol tracing.
    void on_raw_frame(std::function<void(uint32_t can_id, const uint8_t* data, size_t len)> cb);

    // Called by the FasSerial frame callback. Decodes and dispatches.
    void handle_frame(uint32_t can_id, const uint8_t* data, size_t len);

    // ---- Outbound send helpers --------------------------------------------
    // All return false if the serial port is not open.

    bool send_discovery_req();

    // EPB actuator commands
    bool send_pwm_set(uint8_t board_id, uint8_t channel,
                      uint16_t duty_q15, uint16_t period_us = 0);
    bool send_load_sw_set(uint8_t board_id, uint8_t channel,
                          bool enable, uint16_t hold_ms = 0);
    bool send_actuator_failsafe(uint8_t board_id);
    bool send_actuator_query(uint8_t board_id, uint8_t channel);

    // IMC arm / disarm (addressed to the EPB that drives the IMC lines)
    bool send_imc_arm(uint8_t board_id, uint16_t pulse_ms = 0);
    bool send_imc_disarm(uint8_t board_id, uint16_t pulse_ms = 0);

private:
    FasSerial& serial_;
    std::atomic<uint8_t> seq_{0};

    uint8_t next_seq() {
        return seq_.fetch_add(1, std::memory_order_relaxed) & 0xFF;
    }

    // Registered callbacks
    std::function<void(int, const rt_adc_sample_t&)>   cb_adc_sample_;
    std::function<void(int, const rt_board_status_t&)> cb_board_status_;
    std::function<void(int, const rt_imc_status_t&)>   cb_imc_status_;
    std::function<void(const rt_announce_t&)>          cb_announce_;
    std::function<void(int, const rt_heartbeat_t&)>    cb_heartbeat_;
    std::function<void(const rt_fmc_vec3_t&)>          cb_fmc_imu_accel_;
    std::function<void(const rt_fmc_vec3_t&)>          cb_fmc_imu_gyro_;
    std::function<void(const rt_fmc_vec3_t&)>          cb_fmc_accel_hg_;
    std::function<void(const rt_fmc_vec3_t&)>          cb_fmc_mag_;
    std::function<void(const rt_fmc_baro_t&)>          cb_fmc_baro_;
    std::function<void(const rt_fmc_gps_pos_t&)>       cb_fmc_gps_pos_;
    std::function<void(const rt_fmc_gps_info_t&)>      cb_fmc_gps_info_;
    std::function<void(const rt_fmc_health_t&)>        cb_fmc_health_;
    std::function<void(const rt_pmb_pwr_t&)>           cb_pmb_pwr_;
    std::function<void(const rt_pmb_vmon_t&)>          cb_pmb_vmon_;
    std::function<void(const rt_pmb_temp_t&)>          cb_pmb_temp_;
    std::function<void(uint32_t, const uint8_t*, size_t)> cb_raw_frame_;
};
