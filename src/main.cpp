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
#include "controllers/uart_controller.hpp"
#include "core/command_router.hpp"
#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/gpio_manager.hpp"
#include "interfaces/io_expander.hpp"
#include "interfaces/mcc_daqhats.hpp"
#include "interfaces/servo.hpp"
#include "interfaces/uart_link.hpp"

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
const std::string kCommandTopic = "novaground/command";
const std::string kCommandSourceId = "novaOps";
const int kI2CAddr = 0x20;

struct RuntimeConfig {
    std::string node_id = NOVA_BUILD_ID;
    std::string broker = NOVA_DEFAULT_BROKER;
    std::string backend = NOVA_DEFAULT_BACKEND;
    int verbosity = NOVA_DEFAULT_VERBOSITY;
    int sample_interval_ms = NOVA_DEFAULT_SAMPLE_MS;
    int publish_interval_ms = NOVA_DEFAULT_PUBLISH_MS;
    bool enable_uart = true;
    std::string uart_device = "/dev/serial0";
    int uart_baud = 115200;
    bool enable_servo = true;
    bool enable_relay = true;
    bool enable_gpio = true;
};

bool parse_int(const std::string& text, int& value) {
    try {
        size_t idx = 0;
        int parsed = std::stoi(text, &idx);
        if (idx != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

std::string normalize_broker_address(std::string broker) {
    if (broker.find("://") == std::string::npos) {
        if (broker.find(':') == std::string::npos) {
            broker += ":1883";
        }
        broker = "mqtt://" + broker;
    }
    return broker;
}

std::string normalize_backend_base(std::string backend) {
    if (backend.find("://") == std::string::npos) {
        if (backend.find(':') == std::string::npos) {
            backend += ":8000";
        }
        backend = "http://" + backend;
    }
    if (!backend.empty() && backend.back() == '/') {
        backend.pop_back();
    }
    return backend;
}

std::string build_upload_url(const std::string& backend_base) {
    const std::string suffix = "/api/data-files/upload";
    if (backend_base.find(suffix) != std::string::npos) {
        return backend_base;
    }
    return backend_base + suffix;
}

void print_usage(const char* exe_name) {
    std::cout << "Usage: " << exe_name << " [options]\n"
              << "  --broker <host[:port]|mqtt://...>   MQTT broker address\n"
              << "  --backend <host[:port]|http://...>  Backend base URL\n"
              << "  --verbosity <0|1|2>                 0=quiet,1=info,2=debug\n"
              << "  --sample-ms <ms>                    DAQ sampling interval\n"
              << "  --publish-ms <ms>                   Telemetry publish interval\n"
              << "  --uart-device <path>                UART device path (default /dev/serial0)\n"
              << "  --uart-baud <baud>                  UART baud rate (default 115200)\n"
              << "  --no-uart                           Disable STM32 UART link\n"
              << "  --no-servo                          Disable servo/PCA9685 initialization\n"
              << "  --no-relay                          Disable relay/TCA9535 initialization\n"
              << "  --no-gpio                           Disable Raspberry Pi GPIO initialization\n"
              << "  --help                              Show this message\n";
}
}

int main(int argc, char* argv[]) {
    RuntimeConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--broker" && i + 1 < argc) {
            config.broker = argv[++i];
            continue;
        }
        if (arg == "--backend" && i + 1 < argc) {
            config.backend = argv[++i];
            continue;
        }
        if (arg == "--verbosity" && i + 1 < argc) {
            int value = 0;
            if (!parse_int(argv[++i], value)) {
                std::cerr << "Invalid verbosity value.\n";
                return 1;
            }
            config.verbosity = value;
            continue;
        }
        if (arg == "--sample-ms" && i + 1 < argc) {
            int value = 0;
            if (!parse_int(argv[++i], value)) {
                std::cerr << "Invalid sample interval value.\n";
                return 1;
            }
            config.sample_interval_ms = value;
            continue;
        }
        if (arg == "--publish-ms" && i + 1 < argc) {
            int value = 0;
            if (!parse_int(argv[++i], value)) {
                std::cerr << "Invalid publish interval value.\n";
                return 1;
            }
            config.publish_interval_ms = value;
            continue;
        }
        if (arg == "--uart-device" && i + 1 < argc) {
            config.uart_device = argv[++i];
            continue;
        }
        if (arg == "--uart-baud" && i + 1 < argc) {
            int value = 0;
            if (!parse_int(argv[++i], value)) {
                std::cerr << "Invalid UART baud value.\n";
                return 1;
            }
            config.uart_baud = value;
            continue;
        }
        if (arg == "--no-uart") {
            config.enable_uart = false;
            continue;
        }
        if (arg == "--no-servo") {
            config.enable_servo = false;
            continue;
        }
        if (arg == "--no-relay") {
            config.enable_relay = false;
            continue;
        }
        if (arg == "--no-gpio") {
            config.enable_gpio = false;
            continue;
        }
        std::cerr << "Unknown option: " << arg << "\n";
        print_usage(argv[0]);
        return 1;
    }

    if (config.verbosity <= 0) {
        std::cout.setstate(std::ios::failbit);
    }

    std::string broker_address = normalize_broker_address(config.broker);
    std::string backend_base = normalize_backend_base(config.backend);
    std::string upload_url = build_upload_url(backend_base);

    if (config.verbosity >= 2) {
        std::cout << "Build ID: " << config.node_id << std::endl;
        std::cout << "Broker: " << broker_address << std::endl;
        std::cout << "Backend: " << backend_base << std::endl;
        std::cout << "Upload URL: " << upload_url << std::endl;
        std::cout << "Sample interval (ms): " << config.sample_interval_ms << std::endl;
        std::cout << "Publish interval (ms): " << config.publish_interval_ms << std::endl;
        std::cout << "UART: " << (config.enable_uart ? "enabled" : "disabled")
                  << " device=" << config.uart_device
                  << " baud=" << config.uart_baud << std::endl;
        std::cout << "Servo: " << (config.enable_servo ? "enabled" : "disabled")
                  << ", Relay: " << (config.enable_relay ? "enabled" : "disabled")
                  << ", GPIO: " << (config.enable_gpio ? "enabled" : "disabled")
                  << std::endl;
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
    std::unique_ptr<UartLink> uart_link;
    bool has_uart = false;

#ifndef NOVA_MOCK_MODE
    if (config.enable_servo) {
        has_servo = servo_driver.begin();
        if (has_servo) {
            servo_driver.setPWMFreq(50);
        }
    } else {
        std::cout << "Servo initialization disabled." << std::endl;
    }

    if (config.enable_relay) {
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
            has_io_expander = false;
        }
    } else {
        std::cout << "Relay initialization disabled." << std::endl;
    }

    if (config.enable_relay) {
        data_logger.update_relay_state(relay_state);
    }

    if (config.enable_gpio) {
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
    } else {
        std::cout << "GPIO initialization disabled." << std::endl;
    }

    if (config.enable_uart) {
        uart_link = std::make_unique<UartLink>(config.uart_device, config.uart_baud);
        has_uart = uart_link->open();
        if (!has_uart) {
            std::cerr << "UART link unavailable; continuing without STM32 UART." << std::endl;
            uart_link.reset();
        }
    }
#else
    std::cout << "Mock mode enabled: skipping GPIO/relay/servo/UART initialization." << std::endl;
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
    UartController uart_controller(has_uart ? uart_link.get() : nullptr);

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
    router.register_handler("uart", [&uart_controller](const boost::json::object& cmd) {
        uart_controller.handle_command(cmd);
    });

    auto cli = std::make_shared<mqtt::async_client>(broker_address, node_id);

    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(false)
                        .automatic_reconnect(seconds(1), seconds(10))
                        .finalize();

    cli->start_consuming();

    bool connected = false;
    for (int attempt = 0; attempt < 5 && !connected; ++attempt) {
        try {
            auto rsp = cli->connect(connOpts);
            if (rsp) {
                rsp->wait();
                connected = true;
                std::cout << "Connected to MQTT broker" << std::endl;

                auto topics = mqtt::string_collection::create({kCommandTopic});
                const std::vector<int> qos{1};
                cli->subscribe(topics, qos)->wait();
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

    if (has_uart) {
        std::thread uart_rx(uart_rx_loop, std::ref(*uart_link), cli, node_id);
        uart_rx.detach();
    }

    std::thread consumer(consumer_loop, cli, std::ref(router));
    consumer.detach();

    while (true) {
        std::this_thread::sleep_for(seconds(1));
    }
}
