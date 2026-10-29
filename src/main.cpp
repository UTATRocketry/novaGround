#include "mqtt/async_client.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "controllers/data_file_controller.hpp"
#include "controllers/gpio_controller.hpp"
#include "controllers/loops.hpp"
#include "controllers/relay_controller.hpp"
#include "controllers/servo_controller.hpp"
#include "core/command_router.hpp"
#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/gpio_manager.hpp"
#include "interfaces/io_expander.hpp"
#include "interfaces/mcc_daqhats.hpp"
#include "interfaces/servo.hpp"
#include "utils/cli.hpp"

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
const int kI2CAddr = 0x20;
}

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
    const RuntimeConfig& config = parsed.config;

    if (config.verbosity <= 0) {
        std::cout.setstate(std::ios::failbit);
    }

    std::string broker_address = normalize_broker_address(config.broker);
    std::string backend_base = normalize_backend_base(config.backend);
    std::string upload_url = build_upload_url(backend_base);

    // Always say where data is going (issue #29): a wrong backend port used to
    // be invisible until the recordings went missing.
    const char* nova_env = std::getenv("NOVA_ENV");
    std::cout << "Build ID: " << config.node_id
              << "  environment: " << (nova_env && *nova_env ? nova_env : "(not set)") << std::endl;
    std::cout << "Broker: " << broker_address << std::endl;
    std::cout << "Backend: " << backend_base << "  (uploads to " << upload_url << ")" << std::endl;
    check_backend(backend_base);

    if (config.verbosity >= 2) {
        std::cout << "Sample interval (ms): " << config.sample_interval_ms << std::endl;
        std::cout << "Publish interval (ms): " << config.publish_interval_ms << std::endl;
    }

    TelemetryStore telemetry;

    const std::string node_id = config.node_id;

    std::vector<unsigned int> output_pins = {17, 27, 22};
    std::vector<unsigned int> input_pins = {5, 6};

    std::vector<DaqHatDevice> daq_hats;
    bool has_daq = false;

#ifndef NOVA_MOCK_MODE
    try {
        daq_hats = initialize_daqs();
        std::vector<DaqHatDevice> opened_hats;
        for (const auto& hat : daq_hats) {
            if (open_daq_hat(hat)) {
                opened_hats.push_back(hat);
            }
        }
        daq_hats = std::move(opened_hats);
        has_daq = !daq_hats.empty();
    } catch (const std::exception& e) {
        std::cerr << "DAQ initialization failed: " << e.what() << std::endl;
    }
#else
    std::cout << "Mock mode enabled: skipping DAQ initialization." << std::endl;
#endif

    std::vector<std::string> sensor_headers = build_sensor_headers(daq_hats);

    std::vector<std::string> actuator_headers;
    actuator_headers.push_back("timestamp");
    for (unsigned int pin : output_pins) {
        actuator_headers.push_back("gpio_" + std::to_string(pin));
    }
    for (int i = 0; i < 16; ++i) {
        actuator_headers.push_back("relay_" + std::to_string(i));
    }
    for (int i = 0; i < 16; ++i) {
        actuator_headers.push_back("servo_" + std::to_string(i));
    }
    actuator_headers.push_back("type_id");

    std::vector<int> gpio_pins_for_logging;
    gpio_pins_for_logging.reserve(output_pins.size());
    for (unsigned int pin : output_pins) {
        gpio_pins_for_logging.push_back(static_cast<int>(pin));
    }

    DataLogger data_logger("data", sensor_headers, actuator_headers, gpio_pins_for_logging, node_id);

    Adafruit_PWMServoDriver servo_driver;
    bool has_servo = false;
    std::unique_ptr<TCA9535> io_expander;
    bool has_io_expander = false;
    std::bitset<16> relay_state;
    std::unique_ptr<GPIO_Manager> gpio_manager;
    bool has_gpio_manager = false;

#ifndef NOVA_MOCK_MODE
    has_servo = servo_driver.begin();
    if (has_servo) {
        servo_driver.setPWMFreq(50);
    }

    io_expander = std::make_unique<TCA9535>("/dev/i2c-1", kI2CAddr);
    has_io_expander = io_expander->is_ready();
    if (has_io_expander) {
        bool ok = io_expander->configure_port(0, 0x00) &&
                  io_expander->configure_port(1, 0x00);
        for (int i = 0; i < 16; i++) {
            relay_state.set(i, true);
        }
        ok = ok && io_expander->write_output(relay_state);
        if (!ok) {
            std::cerr << "IO expander setup failed; disabling relay control." << std::endl;
            has_io_expander = false;
        }
    } else {
        std::cerr << "IO expander not available." << std::endl;
    }

    data_logger.update_relay_state(relay_state);

    try {
        gpio_manager = std::make_unique<GPIO_Manager>("/dev/gpiochip0");
        has_gpio_manager = true;

        for (auto pin : output_pins) {
            gpio_manager->set_direction(pin, "out");
            gpio_manager->write(pin, 0);
            data_logger.update_gpio(static_cast<int>(pin), 0);
        }

        for (auto pin : input_pins) {
            gpio_manager->set_direction(pin, "in");
        }
    } catch (const std::exception& e) {
        std::cerr << "GPIO Manager initialization failed: " << e.what() << std::endl;
        has_gpio_manager = false;
    }
#else
    std::cout << "Mock mode enabled: skipping GPIO/relay/servo initialization." << std::endl;
#endif

    ServoController servo_controller(has_servo ? &servo_driver : nullptr,
                                     &telemetry,
                                     &data_logger);
    RelayController relay_controller(has_io_expander ? io_expander.get() : nullptr,
                                     &telemetry,
                                     &data_logger);
    if (has_io_expander) {
        relay_controller.set_initial_state(relay_state);
    }
    GpioController gpio_controller(has_gpio_manager ? gpio_manager.get() : nullptr,
                                   &data_logger);
    DataFileController data_file_controller(&data_logger, upload_url);

    CommandRouter router(kCommandSourceId);
    router.register_handler("servo", [&servo_controller](const boost::json::object& cmd) {
        servo_controller.handle_command(cmd);
    });
    router.register_handler("relay", [&relay_controller](const boost::json::object& cmd) {
        relay_controller.handle_command(cmd);
    });
    router.register_handler("gpio", [&gpio_controller](const boost::json::object& cmd) {
        gpio_controller.handle_command(cmd);
    });
    router.register_handler("data_file", [&data_file_controller](const boost::json::object& cmd) {
        data_file_controller.handle_command(cmd);
    });

    auto cli = std::make_shared<mqtt::async_client>(broker_address, node_id);

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

    std::thread publisher(publisher_loop,
                          cli,
                          std::ref(telemetry),
                          node_id,
                          config.publish_interval_ms);
    publisher.detach();

    if (has_daq) {
        std::thread sampler(sample_func,
                            daq_hats,
                            std::ref(telemetry),
                            &data_logger,
                            config.sample_interval_ms);
        sampler.detach();
    }

    if (has_gpio_manager) {
        std::thread gpio_sampler(gpio_sampler_loop, std::ref(*gpio_manager), std::ref(telemetry));
        gpio_sampler.detach();
    }

    std::thread consumer(consumer_loop, cli, std::ref(router));
    consumer.detach();

    while (true) {
        std::this_thread::sleep_for(seconds(1));
    }
}
