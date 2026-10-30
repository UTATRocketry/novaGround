#pragma once

#include "mqtt/async_client.h"

#include <stop_token>
#include <string>

#include "core/command_router.hpp"
#include "core/telemetry.hpp"

// Both loops run until a stop is requested; as the first parameter the token
// is passed automatically by std::jthread.
void publisher_loop(std::stop_token stop,
                    mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms);

void consumer_loop(std::stop_token stop, mqtt::async_client_ptr cli, CommandRouter& router);

