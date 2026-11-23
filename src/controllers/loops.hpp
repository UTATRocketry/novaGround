#pragma once

#include "mqtt/async_client.h"

#include <stop_token>
#include <string>

#include "core/actuator_state.hpp"
#include "core/command_router.hpp"
#include "core/stats.hpp"
#include "core/telemetry.hpp"

// The loops run until a stop is requested; as the first parameter the token
// is passed automatically by std::jthread (which also cannot use default
// arguments, so `stats` is always passed). `stats` may be null. When it is set
// and enabled, each loop records its period ("publisher").

// With stats on, every packet also carries the additive fields `seq` (a per-node
// counter), `sample_ms` (when the newest sample was read) and `publish_ms` (when
// the packet was built), all system-clock milliseconds.
void publisher_loop(std::stop_token stop,
                    mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms,
                    StatsRegistry* stats);

// With stats on, the router times each command from the moment it is consumed
// here (see CommandRouter::set_stats). The loop itself has no period to report:
// it waits on the broker.
void consumer_loop(std::stop_token stop, mqtt::async_client_ptr cli, CommandRouter& router);

// Publishes the registry's stats message (not retained) on stats_topic() every
// interval_ms. Start it only when stats are enabled.
void stats_loop(std::stop_token stop,
                mqtt::async_client_ptr cli,
                StatsRegistry& stats,
                std::string node_id,
                int interval_ms);

// Reads the actuators back from their drivers and publishes the result,
// retained (QoS 1), on state_topic() every interval_ms, starting at once. Start
// it only when stats are enabled; read errors are counted in `stats`.
void state_loop(std::stop_token stop,
                mqtt::async_client_ptr cli,
                ActuatorSources sources,
                StatsRegistry& stats,
                std::string node_id,
                int interval_ms);

// nova/state/<node_id>: what the actuators hold, as read back from the drivers.
std::string state_topic(const std::string& node_id);

// nova/stats/<node_id>: the measurements. The node is the topic, not a field.
std::string stats_topic(const std::string& node_id);

// nova/stats/<node_id>/info: the retained identity and configuration. Publish
// it (retained) every time the client connects.
std::string stats_info_topic(const std::string& node_id);

