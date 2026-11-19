#include "mqtt/async_client.h"

#include <signal.h>

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
#include "core/actuation_guard.hpp"
#include "core/actuator_state.hpp"
#include "core/command_router.hpp"
#include "core/data_logger.hpp"
#include "core/stats.hpp"
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
    // Declared before the controllers and router that report into it.
    std::unique_ptr<StatsRegistry> stats;

    std::vector<unsigned int> output_pins = {17, 27, 22};
    std::vector<unsigned int> input_pins = {5, 6};
    Hardware hardware;
    // --stop-actuate: the drivers the controllers write through. Declared after
    // `hardware`, which they wrap, and before the controllers that use them.
    std::unique_ptr<StoppedRelayBank> stopped_relays;
    std::unique_ptr<StoppedPwmDriver> stopped_servo;
    std::unique_ptr<StoppedDigitalIo> stopped_gpio;

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

    // --stats and --stats-ms were read (and any bad value reported and
    // ignored) by parse_args.
    const bool stats_on = config.stats;
    const int stats_interval_ms = config.stats_interval_ms;
    StatsIdentity identity;
    identity.node = config.node_id;
    identity.build_id = NOVA_BUILD_ID;
    identity.interval_ms = stats_interval_ms;
    identity.actuation = config.stop_actuate ? "stopped" : "enabled";
    app->stats = std::make_unique<StatsRegistry>(std::move(identity), stats_on);
    if (stats_on) {
        std::cout << "Stats: on, every " << stats_interval_ms << " ms to " << stats_topic(config.node_id)
                  << " (identity, retained, on " << stats_info_topic(config.node_id)
                  << "); actuator state, retained, on " << state_topic(config.node_id)
                  << "; seq and timestamps in telemetry" << std::endl;
    } else {
        std::cout << "Stats: off" << std::endl;
    }
    if (config.stop_actuate) {
        // On stderr so --verbosity 0 does not hide it.
        std::cerr << "Actuation: STOPPED (--stop-actuate). Commands are checked, routed and timed, but no\n"
                  << "output is written. Outputs are still set up at startup, as always." << std::endl;
    }
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

    // Register every loop now: the retained info message, published on connect,
    // lists them with their configured periods. (No-ops when stats are off.)
    app->stats->loop("publisher", config.publish_interval_ms);
    if (!hw.daq_hats.empty()) {
        app->stats->loop("sampler", config.sample_interval_ms);
    }
    if (hw.gpio) {
        app->stats->loop("gpio_sampler", kGpioSampleIntervalMs);
    }

    // What the controllers write through: the drivers themselves, or, with
    // --stop-actuate, wrappers that drop every write. Reads (the state topic,
    // the samplers) always go to the real drivers.
    PwmDriver* servo_out = hw.servo.get();
    RelayBank* relays_out = hw.relays.get();
    DigitalIo* gpio_out = hw.gpio.get();
    if (config.stop_actuate) {
        if (hw.servo) {
            app->stopped_servo = std::make_unique<StoppedPwmDriver>(*hw.servo, app->stats.get());
            servo_out = app->stopped_servo.get();
        }
        if (hw.relays) {
            app->stopped_relays = std::make_unique<StoppedRelayBank>(*hw.relays, app->stats.get());
            relays_out = app->stopped_relays.get();
        }
        if (hw.gpio) {
            app->stopped_gpio = std::make_unique<StoppedDigitalIo>(*hw.gpio, app->stats.get());
            gpio_out = app->stopped_gpio.get();
        }
    }

    app->servo_controller = std::make_unique<ServoController>(servo_out, &app->telemetry, app->logger.get());
    app->relay_controller = std::make_unique<RelayController>(relays_out, &app->telemetry, app->logger.get());
    if (hw.relays) {
        app->relay_controller->set_initial_state(hw.relay_state);
    }
    app->gpio_controller = std::make_unique<GpioController>(gpio_out, app->logger.get());
    app->data_file_controller = std::make_unique<DataFileController>(app->logger.get(), app->upload_url);

    app->router = std::make_unique<CommandRouter>(kCommandSourceId);
    app->router->set_stats(app->stats.get());
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
        StatsRegistry* stats = app->stats.get();
        const std::string node_id = config.node_id;
        cli->set_connected_handler([cli, topics, qos, stats, node_id](const std::string& /*cause*/) {
            cli->subscribe(topics, qos);
            std::cout << "Subscribed to " << kCommandTopic << std::endl;
            // Retained, so the broker keeps it for late subscribers; sent on
            // every (re)connect in case a broker restart dropped it. Not waited
            // on: this runs on the client's own callback thread.
            if (stats->enabled()) {
                cli->publish(stats_info_topic(node_id), stats->identity_json(), 1, true);
            }
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
                         app.config.publish_interval_ms,
                         app.stats.get());

    if (!hw.daq_hats.empty()) {
        threads.emplace_back(daq_sample_loop,
                             std::ref(*hw.daq),
                             hw.daq_hats,
                             std::ref(app.telemetry),
                             app.logger.get(),
                             app.config.sample_interval_ms,
                             app.stats.get());
    }

    if (hw.gpio) {
        threads.emplace_back(gpio_sample_loop,
                             std::ref(*hw.gpio),
                             std::ref(app.telemetry),
                             kGpioSampleIntervalMs,
                             app.stats.get());
    }

    threads.emplace_back(consumer_loop, app.cli, std::ref(*app.router));

    if (app.stats->enabled()) {
        threads.emplace_back(stats_loop,
                             app.cli,
                             std::ref(*app.stats),
                             app.config.node_id,
                             app.stats->identity().interval_ms);

        // Read-only: reads the chips' registers and the GPIO lines, writes none.
        ActuatorSources sources;
        sources.relays = hw.relays.get();
        sources.servo = hw.servo.get();
        sources.gpio = hw.gpio.get();
        threads.emplace_back(state_loop,
                             app.cli,
                             sources,
                             std::ref(*app.stats),
                             app.config.node_id,
                             app.stats->identity().interval_ms);
    }

    InterruptibleSleep sleeper;
    while (!sleeper.sleep_for(stop, seconds(1))) {
    }
}

// What is left to do once the loops have stopped and been joined. Writes no
// output: the valves and relays stay as they are, which is the safe state for a
// process that is only being restarted.
void finish(App& app) {
    // The last stats window, in the log and (if the broker is there) on the topic.
    if (app.stats->enabled()) {
        const std::string last = app.stats->take_stats_json();
        std::cout << "Final stats: " << last << std::endl;
        try {
            if (app.cli && app.cli->is_connected()) {
                app.cli->publish(stats_topic(app.config.node_id), last, 1, false)->wait_for(seconds(2));
            }
        } catch (const std::exception& e) {
            std::cerr << "Final stats publish failed: " << e.what() << std::endl;
        }
    }

    // Close the recording, so no file is left half written.
    app.logger->stop();

    try {
        if (app.cli && app.cli->is_connected()) {
            app.cli->stop_consuming();
            app.cli->disconnect()->wait_for(seconds(2));
            std::cout << "Disconnected from MQTT broker" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "MQTT disconnect failed: " << e.what() << std::endl;
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

    // SIGINT, SIGTERM and SIGHUP (systemctl stop, Ctrl+C, and tmux killing the
    // session, which is what run.sh --kill does) are blocked here, before any
    // thread exists so all of them inherit it, and one thread waits for them:
    // a handler could not safely do what stopping needs. It is detached, so
    // that an exception out of build_app does not leave main joining a thread
    // that waits for a signal.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    std::stop_source stop;
    std::thread([signals, stop]() mutable {
        int received = 0;
        if (sigwait(&signals, &received) == 0) {
            std::cerr << "Received signal " << received << ", shutting down." << std::endl;
            stop.request_stop();
        }
    }).detach();

    auto app = build_app(parsed.config);

    run(*app, stop.get_token());
    finish(*app);
    // Destroys the controllers, the logger and the drivers, in that order.
    app.reset();
    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
