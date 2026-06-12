#pragma once

#include <boost/json.hpp>

#include "interfaces/fas_link.hpp"

// Handles MQTT commands of type "fas", routing them to FasLink send helpers.
//
// Accepts two equivalent command shapes:
//
// 1. Direct op shape (low-level):
//      {"type":"fas","op":"pwm_set","board_id":0,"channel":0,"pulse_us":1500,"period_us":20000}
//    Supported ops: pwm_set, load_sw_set, failsafe, imc_arm, imc_disarm, discover, actuator_query
//
// 2. novaOps node/port shape (high-level):
//      {"type":"fas","board_type":"EPB","board_id":1,"port":"relay","channel":2,"action":"on"}
//    port="relay"  action="on"|"off"              → load_sw_set
//    port="servo"  value=<pulse_us>               → pwm_set  (single µs value, period=20000)
//    port="servo"  action="disable"               → pwm_set with pulse_us=0
//    port="servo"  action="enable"                → no-op
//    port="gpio"   action="ARM"|"DISARM"          → imc_arm / imc_disarm
//    Legacy: "node":"EPB_1" accepted in place of board_type/board_id.

class FasController {
public:
    explicit FasController(FasLink* link);

    void handle_command(const boost::json::object& cmd);

private:
    FasLink* link_;

    void handle_node_port_command(const boost::json::object& cmd);
};
