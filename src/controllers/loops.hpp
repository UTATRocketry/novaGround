#pragma once

#include "mqtt/async_client.h"

#include <string>

#include "core/command_router.hpp"
#include "core/telemetry.hpp"
#include "interfaces/gpio_manager.hpp"
#include "interfaces/uart_link.hpp"

void publisher_loop(mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms);

void consumer_loop(mqtt::async_client_ptr cli, CommandRouter& router);

void gpio_sampler_loop(GPIO_Manager& manager, TelemetryStore& telemetry);

void uart_rx_loop(UartLink& uart, mqtt::async_client_ptr cli, std::string source_id);
