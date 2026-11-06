#pragma once

#include "mqtt/async_client.h"

#include <stop_token>
#include <string>

#include "core/command_router.hpp"
#include "core/stats.hpp"
#include "core/telemetry.hpp"

// The loops run until a stop is requested; as the first parameter the token
// is passed automatically by std::jthread (which also cannot use default
// arguments, so `stats` is always passed). `stats` may be null. When it is set
// and enabled, each loop records its period ("publisher", "consumer").

// With stats on, every packet also carries the additive fields `seq` (a per-node
// counter), `sample_ms` (when the newest sample was read) and `publish_ms` (when
// the packet was built), all system-clock milliseconds.
void publisher_loop(std::stop_token stop,
                    mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms,
                    StatsRegistry* stats);

// With stats on, the router also times each command from the moment it is
// consumed here (see CommandRouter::set_stats).
void consumer_loop(std::stop_token stop,
                   mqtt::async_client_ptr cli,
                   CommandRouter& router,
                   StatsRegistry* stats);

// Publishes the registry's stats message on nova/stats/<node_id> every
// interval_ms. Start it only when stats are enabled.
void stats_loop(std::stop_token stop,
                mqtt::async_client_ptr cli,
                StatsRegistry& stats,
                std::string node_id,
                int interval_ms);

// The topic stats_loop publishes to.
std::string stats_topic(const std::string& node_id);

