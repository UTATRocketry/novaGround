#pragma once

#include "mqtt/async_client.h"

#include "core/command_router.hpp"
#include "core/telemetry.hpp"
#include "interfaces/gpio_manager.hpp"

void publisher_loop(mqtt::async_client_ptr cli, TelemetryStore& telemetry);

void consumer_loop(mqtt::async_client_ptr cli, CommandRouter& router);

void gpio_sampler_loop(GPIO_Manager& manager, TelemetryStore& telemetry);
