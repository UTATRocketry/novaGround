#pragma once

#include "mqtt/async_client.h"

#include <string>

#include "core/command_router.hpp"
#include "core/telemetry.hpp"
#include "drivers/linux_gpio.hpp"

void publisher_loop(mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms);

void consumer_loop(mqtt::async_client_ptr cli, CommandRouter& router);

void gpio_sampler_loop(LinuxGpio& manager, TelemetryStore& telemetry);
