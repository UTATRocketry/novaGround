#pragma once

#include "mqtt/async_client.h"

#include <string>

#include "core/command_router.hpp"
#include "core/telemetry.hpp"
#include "interfaces/fas_link.hpp"
#include "interfaces/gpio_manager.hpp"

void publisher_loop(mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms);

void consumer_loop(mqtt::async_client_ptr cli, CommandRouter& router);

void gpio_sampler_loop(GPIO_Manager& manager, TelemetryStore& telemetry);

// Periodically sends DISCOVERY_REQ and marks boards offline after a heartbeat
// timeout (3 s, matching the Python GS). Wakes every 2 s.
void fas_discovery_loop(FasLink& link, TelemetryStore& telemetry);
