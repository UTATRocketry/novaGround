#include "mqtt/async_client.h"

#include <signal.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "controllers/data_file_controller.hpp"
#include "controllers/gpio_controller.hpp"
#include "controllers/io_config_controller.hpp"
#include "controllers/loops.hpp"
#include "controllers/relay_controller.hpp"
#include "controllers/sampler.hpp"
#include "controllers/servo_controller.hpp"
#include "controllers/supervisor_loop.hpp"
#include "core/actuation_guard.hpp"
#include "core/actuator_state.hpp"
#include "core/command_router.hpp"
#include "core/data_logger.hpp"
#include "core/hardware_check.hpp"
#include "core/node_topics.hpp"
#include "core/output_supervisor.hpp"
#include "core/stats.hpp"
#include "core/telemetry.hpp"
#include "interfaces/hardware.hpp"
#include "utils/cli.hpp"
#include "utils/interruptible_sleep.hpp"
#include "utils/sd_notify.hpp"

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

// The node this build takes commands and config for. Its own name, except in a
// build that stands in for another node: novaMock sets
// NOVA_STANDS_IN_FOR="novaGround" (meson.build), so it acts on novaGround's
// commands and config while keeping its own name everywhere else (client ID,
// telemetry source, stats and state topics, CSV names). Identity, like
// NOVA_BUILD_ID, not a setting.
#ifdef NOVA_STANDS_IN_FOR
#define NOVA_ROUTING_NODE NOVA_STANDS_IN_FOR
#else
#define NOVA_ROUTING_NODE NOVA_BUILD_ID
#endif

// The hardware this build is made for, besides GPIO, which every build needs
// (core/hardware_check.hpp). Part of what the build is, set next to
// NOVA_BUILD_ID in meson.build; a missing part is reported, never enforced.
#ifndef NOVA_EXPECTED_HARDWARE
#define NOVA_EXPECTED_HARDWARE ""
#endif

namespace {
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
    NodeTopics topics;

    Hardware hardware;
    // --stop-actuate: the drivers the controllers write through. Declared after
    // `hardware`, which they wrap, and before the controllers that use them.
    std::unique_ptr<StoppedRelayBank> stopped_relays;
    std::unique_ptr<StoppedPwmDriver> stopped_servo;
    std::unique_ptr<StoppedDigitalIo> stopped_gpio;
    // The output limits, and the relay and GPIO drivers wrapped by them (above
    // the --stop-actuate wrappers, so a trip is dropped by those too).
    std::unique_ptr<OutputSupervisor> supervisor;
    std::unique_ptr<SupervisedRelayBank> supervised_relays;
    std::unique_ptr<SupervisedDigitalIo> supervised_gpio;

    std::unique_ptr<DataLogger> logger;
    std::unique_ptr<ServoController> servo_controller;
    std::unique_ptr<RelayController> relay_controller;
    std::unique_ptr<GpioController> gpio_controller;
    std::unique_ptr<IoConfigController> io_config;
    std::unique_ptr<DataFileController> data_file_controller;
    std::unique_ptr<CommandRouter> router;
    std::unique_ptr<MessageDispatcher> dispatcher;
    std::shared_ptr<mqtt::async_client> cli;
};

// $NOVA_STATE_DIR/hardware.json: what this build expects and found, for
// nova-pi.sh status. Written at each start; without NOVA_STATE_DIR, nothing is.
void write_hardware_state(const hardware_check::Report& report) {
    const char* dir = std::getenv("NOVA_STATE_DIR");
    if (!dir || !*dir) {
        return;
    }
    boost::json::object doc = report.to_json();
    char now[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(now, sizeof now, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    doc["time"] = now;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path target = std::filesystem::path(dir) / "hardware.json";
    const std::filesystem::path tmp = std::filesystem::path(dir) / "hardware.json.tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            std::cerr << "Could not write " << tmp << std::endl;
            return;
        }
        out << boost::json::serialize(doc) << "
";
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::cerr << "Could not write " << target << ": " << ec.message() << std::endl;
    }
}

// Brings up the hardware, the logger, the controllers and the MQTT client
// (connect_broker connects it).
// Starts no threads: that is run(). Returns null if this Pi has no GPIO, the
// one piece of hardware every build needs.
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
    // The hardware first: what it found goes in the stats info message. A
    // missing part is logged and left out; only missing GPIO stops the program.
    app->hardware = build_hardware();
    hardware_check::Found found;
    found.daq_hats = app->hardware.daq_hats;
    found.relays = app->hardware.relays != nullptr;
    found.servo = app->hardware.servo != nullptr;
    found.gpio = app->hardware.gpio != nullptr;
    const hardware_check::Report hw_report =
        hardware_check::check(NOVA_BUILD_ID, NOVA_EXPECTED_HARDWARE, found);
    const auto missing = hw_report.missing();
    // On stderr when something is missing, so --verbosity 0 does not hide it.
    std::ostream& hw_out = missing.empty() ? std::cout : std::cerr;
    hw_out << "Hardware for " << NOVA_BUILD_ID << ":" << std::endl;
    for (const auto& line : hw_report.lines()) {
        hw_out << line << std::endl;
    }
    if (!missing.empty()) {
        std::string list;
        for (const auto& name : missing) {
            list += (list.empty() ? "" : ", ") + name;
        }
        std::cerr << "HARDWARE MISSING: " << list << " (expected on " << NOVA_BUILD_ID << ")" << std::endl;
    }
    write_hardware_state(hw_report);
    if (!hw_report.gpio_ok()) {
        return nullptr;
    }

    const bool stats_on = config.stats;
    const int stats_interval_ms = config.stats_interval_ms;
    StatsIdentity identity;
    identity.node = config.node_id;
    identity.build_id = NOVA_BUILD_ID;
    identity.interval_ms = stats_interval_ms;
    identity.actuation = config.stop_actuate ? "stopped" : "enabled";
    identity.hardware_json = boost::json::serialize(hw_report.to_json());
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
                  << "output is written, and no GPIO output is claimed. The relays are still set to their\n"
                  << "startup state (de-energized), as always." << std::endl;
    }
    std::cout << "Broker: " << broker_address << std::endl;
    std::cout << "Backend: " << backend_base << "  (uploads to " << app->upload_url << ")" << std::endl;
    check_backend(backend_base);

    if (config.verbosity >= 2) {
        std::cout << "Sample interval (ms): " << config.sample_interval_ms << std::endl;
        std::cout << "Publish interval (ms): " << config.publish_interval_ms << std::endl;
    }

    app->topics = NodeTopics::for_node(NOVA_ROUTING_NODE);
    if (std::string(NOVA_ROUTING_NODE) != config.node_id) {
        std::cout << "Stands in for " << NOVA_ROUTING_NODE << ": commands on " << app->topics.command_node
                  << ", config on " << app->topics.config << std::endl;
    }

    Hardware& hw = app->hardware;

    // No GPIO output is configured until the config arrives, so a recording
    // started before it has no GPIO columns (IoConfigController sets them).
    app->logger = std::make_unique<DataLogger>("data",
                                               build_sensor_headers(hw.daq_hats),
                                               actuator_log_headers({}),
                                               std::vector<int>{},
                                               config.node_id);

    // Tell the logger the state the hardware started in.
    app->logger->update_relay_state(hw.relay_state);

    // Register every loop now: the retained info message, published on connect,
    // lists them with their configured periods. (No-ops when stats are off.)
    app->stats->loop("publisher", config.publish_interval_ms);
    if (!hw.daq_hats.empty()) {
        app->stats->loop("sampler", config.sample_interval_ms);
    }
    if (hw.gpio) {
        app->stats->loop("gpio_sampler", kGpioSampleIntervalMs);
    }
    app->stats->loop("supervisor", kSupervisorIntervalMs);

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

    app->supervisor = std::make_unique<OutputSupervisor>();
    app->supervisor->set_stats(app->stats.get());
    if (relays_out) {
        app->supervised_relays = std::make_unique<SupervisedRelayBank>(
            *relays_out, *app->supervisor, hw.relay_state, app->stats.get());
        relays_out = app->supervised_relays.get();
    }
    if (gpio_out) {
        app->supervised_gpio = std::make_unique<SupervisedDigitalIo>(*gpio_out, *app->supervisor, app->stats.get());
        gpio_out = app->supervised_gpio.get();
    }

    app->servo_controller = std::make_unique<ServoController>(servo_out, &app->telemetry, app->logger.get());
    app->relay_controller = std::make_unique<RelayController>(relays_out, &app->telemetry, app->logger.get());
    if (hw.relays) {
        app->relay_controller->set_initial_state(hw.relay_state);
    }
    app->relay_controller->set_supervisor(app->supervisor.get());
    app->gpio_controller = std::make_unique<GpioController>(gpio_out, app->logger.get(), app->stats.get());
    app->gpio_controller->set_supervisor(app->supervisor.get());
    app->data_file_controller = std::make_unique<DataFileController>(app->logger.get(), app->upload_url);
    app->io_config = std::make_unique<IoConfigController>(app->gpio_controller.get(),
                                                          hw.daq.get(),
                                                          hw.daq_hats,
                                                          app->supervisor.get(),
                                                          app->logger.get());

    ServoController* servo = app->servo_controller.get();
    RelayController* relay = app->relay_controller.get();
    GpioController* gpio = app->gpio_controller.get();
    DataFileController* data_file = app->data_file_controller.get();
    IoConfigController* io_config = app->io_config.get();
    OutputSupervisor* supervisor = app->supervisor.get();

    // A trip goes through the output's controller, so its state, the telemetry
    // and the actuator log stay right.
    supervisor->set_trip_handler([relay, gpio](const OutputId& output, const std::string& reason) {
        if (output.kind == OutputKind::Gpio) {
            gpio->drive_safe(output.index, reason);
        } else {
            relay->drive_safe(output.index, reason);
        }
    });

    app->router = std::make_unique<CommandRouter>(kCommandSourceId);
    app->router->set_stats(app->stats.get());
    // relay, gpio and servo drive this node's outputs: accepted only on
    // nova/command/<node>. data_file is for every node.
    app->router->register_handler("servo", [servo](const boost::json::object& cmd) {
        servo->handle_command(cmd);
    }, true);
    app->router->register_handler("relay", [relay](const boost::json::object& cmd) {
        relay->handle_command(cmd);
    }, true);
    app->router->register_handler("gpio", [gpio](const boost::json::object& cmd) {
        gpio->handle_command(cmd);
    }, true);
    app->router->register_handler("data_file", [data_file](const boost::json::object& cmd) {
        data_file->handle_command(cmd);
    });

    app->dispatcher = std::make_unique<MessageDispatcher>(
        app->topics,
        *app->router,
        [io_config](const std::string& payload) { io_config->handle_message(payload); },
        [supervisor](const std::string& payload, bool retained) { supervisor->heartbeat(payload, retained); });

    app->cli = std::make_shared<mqtt::async_client>(broker_address, config.node_id);
    auto cli = app->cli;

    {
        const std::vector<std::string> subscriptions = app->topics.subscriptions();
        auto topics = mqtt::string_collection::create(subscriptions);
        const std::vector<int> qos(subscriptions.size(), 1);
        StatsRegistry* stats = app->stats.get();
        const std::string node_id = config.node_id;
        const std::string status_topic = app->topics.config_status;

        // The config status, retained, whenever it changes and on every
        // (re)connect. Not waited on: this can run on the client's callback
        // thread.
        std::weak_ptr<mqtt::async_client> weak_cli = cli;
        io_config->set_publisher([weak_cli, status_topic](const std::string& status) {
            auto client = weak_cli.lock();
            if (client && client->is_connected()) {
                client->publish(status_topic, status, 1, true);
            }
        });

        cli->set_connected_handler([cli, topics, qos, stats, node_id, subscriptions, io_config](
                                       const std::string& /*cause*/) {
            cli->subscribe(topics, qos);
            for (const auto& topic : subscriptions) {
                std::cout << "Subscribed to " << topic << std::endl;
            }
            // Retained, so the broker keeps it for late subscribers; sent on
            // every (re)connect in case a broker restart dropped it. Not waited
            // on: this runs on the client's own callback thread.
            if (stats->enabled()) {
                cli->publish(stats_info_topic(node_id), stats->identity_json(), 1, true);
            }
            io_config->publish_status();
        });
        // A lost connection is a comms loss for the output supervisor, until
        // the next live heartbeat after reconnecting.
        cli->set_connection_lost_handler([supervisor](const std::string& cause) {
            std::cerr << "MQTT connection lost" << (cause.empty() ? "" : ": " + cause) << std::endl;
            supervisor->connection_lost();
        });
    }

    return app;
}

// Connects to the broker: five attempts, then carries on without it (paho's
// automatic reconnect takes over once a connection has been made). Blocks for as
// long as the attempts take, so run() calls it after the supervisor thread is
// running and systemd has been told READY=1.
void connect_broker(App& app) {
    auto& cli = app.cli;
    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(true)
                        .automatic_reconnect(seconds(1), seconds(10))
                        .finalize();

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
}

// Starts the loops and runs until `stop` is requested, then stops and joins
// them (a std::jthread asks its thread to stop when it is destroyed).
void run(App& app, std::stop_token stop) {
    std::vector<std::jthread> threads;
    Hardware& hw = app.hardware;

    // Max on-time and comms-loss trips. Each pass also publishes the config
    // status when the trips changed, and pings systemd's watchdog
    // (WatchdogSec in novaGround@.service): if this thread stalls, the pings
    // stop and systemd restarts the process.
    {
        OutputSupervisor* supervisor = app.supervisor.get();
        IoConfigController* io_config = app.io_config.get();
        threads.emplace_back(supervisor_loop,
                             std::ref(*app.supervisor),
                             std::function<void()>([supervisor, io_config]() {
                                 if (supervisor->take_changed()) {
                                     io_config->publish_status();
                                 }
                                 sd_notify_send("WATCHDOG=1");
                             }),
                             kSupervisorIntervalMs,
                             app.stats.get());
    }

    // Startup is done: no GPIO pin is claimed and the relays are de-energized
    // until the config arrives, and the supervisor is pinging the watchdog.
    // (Nothing happens without NOTIFY_SOCKET.)
    if (sd_notify_send("READY=1")) {
        std::cout << "Told systemd we are ready" << std::endl;
    }

    connect_broker(app);

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

    threads.emplace_back(consumer_loop, app.cli, std::ref(*app.dispatcher));

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
    sd_notify_send("STOPPING=1");
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
    if (!app) {
        std::cerr << "No GPIO on this Pi (/dev/gpiochip0): stopping." << std::endl;
        return 1;
    }

    run(*app, stop.get_token());
    finish(*app);
    // Destroys the controllers, the logger and the drivers, in that order.
    app.reset();
    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
