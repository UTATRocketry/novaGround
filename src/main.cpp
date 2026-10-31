#include "mqtt/async_client.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "controllers/data_file_controller.hpp"
#include "controllers/gpio_controller.hpp"
#include "controllers/loops.hpp"
#include "controllers/relay_controller.hpp"
#include "controllers/sampler.hpp"
#include "controllers/servo_controller.hpp"
#include "core/command_router.hpp"
#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/hardware.hpp"
#include "utils/cli.hpp"
#include "utils/interruptible_sleep.hpp"

using namespace std::chrono;

#ifndef NOVA_BUILD_ID
#define NOVA_BUILD_ID "novaGround"
#endif
#ifndef NOVA_DEFAULT_BROKER
#define NOVA_DEFAULT_BROKER "localhost:1883"
#endif
#ifndef NOVA_DEFAULT_BACKEND
#define NOVA_DEFAULT_BACKEND "http://localhost:8000"
#endif
#ifndef NOVA_DEFAULT_VERBOSITY
#define NOVA_DEFAULT_VERBOSITY 1
#endif
#ifndef NOVA_DEFAULT_SAMPLE_MS
#define NOVA_DEFAULT_SAMPLE_MS 1
#endif
#ifndef NOVA_DEFAULT_PUBLISH_MS
#define NOVA_DEFAULT_PUBLISH_MS 50
#endif

namespace {
const std::string kCommandTopic = "nova/command";
const std::string kCommandSourceId = "novaOps";
const int kGpioSampleIntervalMs = 50;

// Everything the program owns once it is built. Members are destroyed in
// reverse order, so each one outlives what points at it. Not movable: the
// controllers and the router hold pointers into it.
struct App {
    RuntimeConfig config;
    std::string upload_url;
    TelemetryStore telemetry;

    std::vector<unsigned int> output_pins = {17, 27, 22};
    std::vector<unsigned int> input_pins = {5, 6};
    Hardware hardware;

    std::unique_ptr<DataLogger> logger;
    std::unique_ptr<ServoController> servo_controller;
    std::unique_ptr<RelayController> relay_controller;
    std::unique_ptr<GpioController> gpio_controller;
    std::unique_ptr<DataFileController> data_file_controller;
    std::unique_ptr<CommandRouter> router;
    std::shared_ptr<mqtt::async_client> cli;
};

std::vector<std::string> actuator_log_headers(const std::vector<unsigned int>& output_pins) {
    std::vector<std::string> headers;
    headers.push_back("timestamp");
    for (unsigned int pin : output_pins) {
        headers.push_back("gpio_" + std::to_string(pin));
    }
    for (int i = 0; i < 16; ++i) {
        headers.push_back("relay_" + std::to_string(i));
    }
    for (int i = 0; i < 16; ++i) {
        headers.push_back("servo_" + std::to_string(i));
    }
    headers.push_back("type_id");
    return headers;
}

// Brings up the hardware, the logger, the controllers and the MQTT connection.
// Starts no threads: that is run().
std::unique_ptr<App> build_app(const RuntimeConfig& config) {
    auto app = std::make_unique<App>();
    app->config = config;

    const std::string broker_address = normalize_broker_address(config.broker);
    const std::string backend_base = normalize_backend_base(config.backend);
    app->upload_url = build_upload_url(backend_base);

    // Always say where data is going (issue #29): a wrong backend port used to
    // be invisible until the recordings went missing.
    const char* nova_env = std::getenv("NOVA_ENV");
    std::cout << "Build ID: " << config.node_id
              << "  environment: " << (nova_env && *nova_env ? nova_env : "(not set)") << std::endl;
    std::cout << "Broker: " << broker_address << std::endl;
    std::cout << "Backend: " << backend_base << "  (uploads to " << app->upload_url << ")" << std::endl;
    check_backend(backend_base);

    if (config.verbosity >= 2) {
        std::cout << "Sample interval (ms): " << config.sample_interval_ms << std::endl;
        std::cout << "Publish interval (ms): " << config.publish_interval_ms << std::endl;
    }

    app->hardware = build_hardware(app->output_pins, app->input_pins);
    Hardware& hw = app->hardware;

    std::vector<int> gpio_pins_for_logging;
    gpio_pins_for_logging.reserve(app->output_pins.size());
    for (unsigned int pin : app->output_pins) {
        gpio_pins_for_logging.push_back(static_cast<int>(pin));
    }
    app->logger = std::make_unique<DataLogger>("data",
                                               build_sensor_headers(hw.daq_hats),
                                               actuator_log_headers(app->output_pins),
                                               gpio_pins_for_logging,
                                               config.node_id);

    // Tell the logger the state the hardware started in.
    app->logger->update_relay_state(hw.relay_state);
    if (hw.gpio) {
        for (int pin : gpio_pins_for_logging) {
            app->logger->update_gpio(pin, 0);
        }
    }

    app->servo_controller = std::make_unique<ServoController>(hw.servo.get(), &app->telemetry, app->logger.get());
    app->relay_controller = std::make_unique<RelayController>(hw.relays.get(), &app->telemetry, app->logger.get());
    if (hw.relays) {
        app->relay_controller->set_initial_state(hw.relay_state);
    }
    app->gpio_controller = std::make_unique<GpioController>(hw.gpio.get(), app->logger.get());
    app->data_file_controller = std::make_unique<DataFileController>(app->logger.get(), app->upload_url);

    app->router = std::make_unique<CommandRouter>(kCommandSourceId);
    ServoController* servo = app->servo_controller.get();
    RelayController* relay = app->relay_controller.get();
    GpioController* gpio = app->gpio_controller.get();
    DataFileController* data_file = app->data_file_controller.get();
    app->router->register_handler("servo", [servo](const boost::json::object& cmd) {
        servo->handle_command(cmd);
    });
    app->router->register_handler("relay", [relay](const boost::json::object& cmd) {
        relay->handle_command(cmd);
    });
    app->router->register_handler("gpio", [gpio](const boost::json::object& cmd) {
        gpio->handle_command(cmd);
    });
    app->router->register_handler("data_file", [data_file](const boost::json::object& cmd) {
        data_file->handle_command(cmd);
    });

    app->cli = std::make_shared<mqtt::async_client>(broker_address, config.node_id);
    auto cli = app->cli;

    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(true)
                        .automatic_reconnect(seconds(1), seconds(10))
                        .finalize();

    {
        auto topics = mqtt::string_collection::create({kCommandTopic});
        const std::vector<int> qos{1};
        cli->set_connected_handler([cli, topics, qos](const std::string& /*cause*/) {
            cli->subscribe(topics, qos);
            std::cout << "Subscribed to " << kCommandTopic << std::endl;
        });
    }

    cli->start_consuming();

    bool connected = false;
    for (int attempt = 0; attempt < 5 && !connected; ++attempt) {
        try {
            auto rsp = cli->connect(connOpts);
            if (rsp) {
                auto connResponse = rsp->get_connect_response();
                connected = true;
                std::cout << "Connected to MQTT broker" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "MQTT connect failed: " << e.what() << std::endl;
        }

        if (!connected) {
            std::this_thread::sleep_for(seconds(2));
        }
    }

    if (!connected) {
        std::cerr << "MQTT connection unavailable; continuing without broker." << std::endl;
    }

    return app;
}

// Starts the loops and runs until `stop` is requested, then stops and joins
// them (a std::jthread asks its thread to stop when it is destroyed).
void run(App& app, std::stop_token stop) {
    std::vector<std::jthread> threads;
    Hardware& hw = app.hardware;

    threads.emplace_back(publisher_loop,
                         app.cli,
                         std::ref(app.telemetry),
                         app.config.node_id,
                         app.config.publish_interval_ms);

    if (!hw.daq_hats.empty()) {
        threads.emplace_back(daq_sample_loop,
                             std::ref(*hw.daq),
                             hw.daq_hats,
                             std::ref(app.telemetry),
                             app.logger.get(),
                             app.config.sample_interval_ms);
    }

    if (hw.gpio) {
        threads.emplace_back(gpio_sample_loop,
                             std::ref(*hw.gpio),
                             std::ref(app.telemetry),
                             kGpioSampleIntervalMs);
    }

    threads.emplace_back(consumer_loop, app.cli, std::ref(*app.router));

    InterruptibleSleep sleeper;
    while (!sleeper.sleep_for(stop, seconds(1))) {
    }
}
}  // namespace

int main(int argc, char* argv[]) {
    RuntimeConfig defaults;
    defaults.node_id = NOVA_BUILD_ID;
    defaults.broker = NOVA_DEFAULT_BROKER;
    defaults.backend = NOVA_DEFAULT_BACKEND;
    defaults.verbosity = NOVA_DEFAULT_VERBOSITY;
    defaults.sample_interval_ms = NOVA_DEFAULT_SAMPLE_MS;
    defaults.publish_interval_ms = NOVA_DEFAULT_PUBLISH_MS;

    ParseResult parsed = parse_args(std::vector<std::string>(argv + 1, argv + argc),
                                    defaults, argv[0], std::cout, std::cerr);
    if (parsed.status == ParseStatus::ExitOk) {
        return 0;
    }
    if (parsed.status == ParseStatus::ExitError) {
        return 1;
    }

    if (parsed.config.verbosity <= 0) {
        std::cout.setstate(std::ios::failbit);
    }

    auto app = build_app(parsed.config);

    std::stop_source stop;
    run(*app, stop.get_token());
    return 0;
}
