#include "fas_link.hpp"

#include <cstring>
#include <iostream>

FasLink::FasLink(FasSerial& serial) : serial_(serial) {}

// ---- Callback registration -----------------------------------------------

void FasLink::on_adc_sample(std::function<void(int, const rt_adc_sample_t&)> cb)
    { cb_adc_sample_ = std::move(cb); }
void FasLink::on_board_status(std::function<void(int, const rt_board_status_t&)> cb)
    { cb_board_status_ = std::move(cb); }
void FasLink::on_imc_status(std::function<void(int, const rt_imc_status_t&)> cb)
    { cb_imc_status_ = std::move(cb); }
void FasLink::on_announce(std::function<void(const rt_announce_t&)> cb)
    { cb_announce_ = std::move(cb); }
void FasLink::on_heartbeat(std::function<void(int, const rt_heartbeat_t&)> cb)
    { cb_heartbeat_ = std::move(cb); }
void FasLink::on_fmc_imu_accel(std::function<void(const rt_fmc_vec3_t&)> cb)
    { cb_fmc_imu_accel_ = std::move(cb); }
void FasLink::on_fmc_imu_gyro(std::function<void(const rt_fmc_vec3_t&)> cb)
    { cb_fmc_imu_gyro_ = std::move(cb); }
void FasLink::on_fmc_accel_hg(std::function<void(const rt_fmc_vec3_t&)> cb)
    { cb_fmc_accel_hg_ = std::move(cb); }
void FasLink::on_fmc_mag(std::function<void(const rt_fmc_vec3_t&)> cb)
    { cb_fmc_mag_ = std::move(cb); }
void FasLink::on_fmc_baro(std::function<void(const rt_fmc_baro_t&)> cb)
    { cb_fmc_baro_ = std::move(cb); }
void FasLink::on_fmc_gps_pos(std::function<void(const rt_fmc_gps_pos_t&)> cb)
    { cb_fmc_gps_pos_ = std::move(cb); }
void FasLink::on_fmc_gps_info(std::function<void(const rt_fmc_gps_info_t&)> cb)
    { cb_fmc_gps_info_ = std::move(cb); }
void FasLink::on_fmc_health(std::function<void(const rt_fmc_health_t&)> cb)
    { cb_fmc_health_ = std::move(cb); }
void FasLink::on_pmb_pwr(std::function<void(const rt_pmb_pwr_t&)> cb)
    { cb_pmb_pwr_ = std::move(cb); }
void FasLink::on_pmb_vmon(std::function<void(const rt_pmb_vmon_t&)> cb)
    { cb_pmb_vmon_ = std::move(cb); }
void FasLink::on_pmb_temp(std::function<void(const rt_pmb_temp_t&)> cb)
    { cb_pmb_temp_ = std::move(cb); }
void FasLink::on_raw_frame(std::function<void(uint32_t, const uint8_t*, size_t)> cb)
    { cb_raw_frame_ = std::move(cb); }

// ---- Frame dispatch -------------------------------------------------------

void FasLink::handle_frame(uint32_t can_id, const uint8_t* data, size_t len) {
    if (cb_raw_frame_) cb_raw_frame_(can_id, data, len);

    rt_can_id_t cid = rt_can_id_unpack(can_id);
    auto msg = static_cast<rt_msg_t>(cid.msg);

    switch (msg) {

        case RT_MSG_HEARTBEAT:
            if (len >= sizeof(rt_heartbeat_t) && cb_heartbeat_) {
                rt_heartbeat_t hb;
                std::memcpy(&hb, data, sizeof(hb));
                cb_heartbeat_(cid.board_id, hb);
            }
            break;

        case RT_MSG_DISCOVERY_ANNOUNCE:
            if (len >= sizeof(rt_announce_t) && cb_announce_) {
                rt_announce_t ann;
                std::memcpy(&ann, data, sizeof(ann));
                cb_announce_(ann);
            }
            break;

        case RT_MSG_ADC_SAMPLE:
            if (len >= sizeof(rt_adc_sample_t) && cb_adc_sample_) {
                rt_adc_sample_t s;
                std::memcpy(&s, data, sizeof(s));
                cb_adc_sample_(cid.board_id, s);
            }
            break;

        case RT_MSG_BOARD_STATUS:
            if (len >= sizeof(rt_board_status_t) && cb_board_status_) {
                rt_board_status_t bs;
                std::memcpy(&bs, data, sizeof(bs));
                cb_board_status_(cid.board_id, bs);
            }
            break;

        case RT_MSG_IMC_STATUS:
            if (len >= sizeof(rt_imc_status_t) && cb_imc_status_) {
                rt_imc_status_t st;
                std::memcpy(&st, data, sizeof(st));
                cb_imc_status_(cid.board_id, st);
            }
            break;

        case RT_MSG_FMC_IMU_ACCEL:
            if (len >= sizeof(rt_fmc_vec3_t) && cb_fmc_imu_accel_) {
                rt_fmc_vec3_t v;
                std::memcpy(&v, data, sizeof(v));
                cb_fmc_imu_accel_(v);
            }
            break;

        case RT_MSG_FMC_IMU_GYRO:
            if (len >= sizeof(rt_fmc_vec3_t) && cb_fmc_imu_gyro_) {
                rt_fmc_vec3_t v;
                std::memcpy(&v, data, sizeof(v));
                cb_fmc_imu_gyro_(v);
            }
            break;

        case RT_MSG_FMC_ACCEL_HG:
            if (len >= sizeof(rt_fmc_vec3_t) && cb_fmc_accel_hg_) {
                rt_fmc_vec3_t v;
                std::memcpy(&v, data, sizeof(v));
                cb_fmc_accel_hg_(v);
            }
            break;

        case RT_MSG_FMC_MAG:
            if (len >= sizeof(rt_fmc_vec3_t) && cb_fmc_mag_) {
                rt_fmc_vec3_t v;
                std::memcpy(&v, data, sizeof(v));
                cb_fmc_mag_(v);
            }
            break;

        case RT_MSG_FMC_BARO:
            if (len >= sizeof(rt_fmc_baro_t) && cb_fmc_baro_) {
                rt_fmc_baro_t b;
                std::memcpy(&b, data, sizeof(b));
                cb_fmc_baro_(b);
            }
            break;

        case RT_MSG_FMC_GPS_POS:
            if (len >= sizeof(rt_fmc_gps_pos_t) && cb_fmc_gps_pos_) {
                rt_fmc_gps_pos_t g;
                std::memcpy(&g, data, sizeof(g));
                cb_fmc_gps_pos_(g);
            }
            break;

        case RT_MSG_FMC_GPS_INFO:
            if (len >= sizeof(rt_fmc_gps_info_t) && cb_fmc_gps_info_) {
                rt_fmc_gps_info_t g;
                std::memcpy(&g, data, sizeof(g));
                cb_fmc_gps_info_(g);
            }
            break;

        case RT_MSG_FMC_HEALTH:
            if (len >= sizeof(rt_fmc_health_t) && cb_fmc_health_) {
                rt_fmc_health_t h;
                std::memcpy(&h, data, sizeof(h));
                cb_fmc_health_(h);
            }
            break;

        case RT_MSG_PMB_PWR:
            if (len >= sizeof(rt_pmb_pwr_t) && cb_pmb_pwr_) {
                rt_pmb_pwr_t p;
                std::memcpy(&p, data, sizeof(p));
                cb_pmb_pwr_(p);
            }
            break;

        case RT_MSG_PMB_VMON:
            if (len >= sizeof(rt_pmb_vmon_t) && cb_pmb_vmon_) {
                rt_pmb_vmon_t v;
                std::memcpy(&v, data, sizeof(v));
                cb_pmb_vmon_(v);
            }
            break;

        case RT_MSG_PMB_TEMP:
            if (len >= sizeof(rt_pmb_temp_t) && cb_pmb_temp_) {
                rt_pmb_temp_t t;
                std::memcpy(&t, data, sizeof(t));
                cb_pmb_temp_(t);
            }
            break;

        default:
            break; // silently ignore unhandled message types
    }
}

// ---- Outbound send helpers ------------------------------------------------

bool FasLink::send_discovery_req() {
    uint32_t cid = rt_can_id_pack(RT_MSG_DISCOVERY_REQ,
                                   RT_BOARD_GS, 0, 0, 0, next_seq());
    uint8_t pad[8] = {};
    return serial_.send_frame(cid, pad, sizeof(pad));
}

bool FasLink::send_pwm_set(uint8_t board_id, uint8_t channel,
                            uint16_t duty_q15, uint16_t period_us) {
    uint32_t cid = rt_can_id_pack(RT_MSG_PWM_SET,
                                   RT_BOARD_GS, board_id, channel, 0, next_seq());
    rt_pwm_set_t payload{};
    payload.duty_q15  = duty_q15;
    payload.period_us = period_us;
    return serial_.send_frame(cid,
        reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
}

bool FasLink::send_load_sw_set(uint8_t board_id, uint8_t channel,
                                bool enable, uint16_t hold_ms) {
    uint32_t cid = rt_can_id_pack(RT_MSG_LOAD_SW_SET,
                                   RT_BOARD_GS, board_id, channel, 0, next_seq());
    rt_load_sw_set_t payload{};
    payload.enable  = enable ? 1 : 0;
    payload.hold_ms = hold_ms;
    return serial_.send_frame(cid,
        reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
}

bool FasLink::send_actuator_failsafe(uint8_t board_id) {
    uint32_t cid = rt_can_id_pack(RT_MSG_ACTUATOR_FAILSAFE,
                                   RT_BOARD_GS, board_id, 0, 0, next_seq());
    uint8_t pad[8] = {};
    return serial_.send_frame(cid, pad, sizeof(pad));
}

bool FasLink::send_actuator_query(uint8_t board_id, uint8_t channel) {
    uint32_t cid = rt_can_id_pack(RT_MSG_ACTUATOR_QUERY,
                                   RT_BOARD_GS, board_id, channel, 0, next_seq());
    uint8_t pad[8] = {};
    return serial_.send_frame(cid, pad, sizeof(pad));
}

bool FasLink::send_imc_arm(uint8_t board_id, uint16_t pulse_ms) {
    uint32_t cid = rt_can_id_pack(RT_MSG_IGN_ARM,
                                   RT_BOARD_GS, board_id, 0, 0, next_seq());
    rt_imc_cmd_t payload{};
    payload.pulse_ms = pulse_ms;
    return serial_.send_frame(cid,
        reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
}

bool FasLink::send_imc_disarm(uint8_t board_id, uint16_t pulse_ms) {
    uint32_t cid = rt_can_id_pack(RT_MSG_IGN_DISARM,
                                   RT_BOARD_GS, board_id, 0, 0, next_seq());
    rt_imc_cmd_t payload{};
    payload.pulse_ms = pulse_ms;
    return serial_.send_frame(cid,
        reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
}
