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

#include <daqhats/mcc128.h>
#include <daqhats/daqhats.h>

#include "controllers/gpio_controller.hpp"
#include "controllers/loops.hpp"
#include "controllers/relay_controller.hpp"
#include "controllers/servo_controller.hpp"
#include "core/command_router.hpp"
#include "core/data_file.hpp"
#include "core/telemetry.hpp"
#include "interfaces/gpio_manager.hpp"
#include "interfaces/io_expander.hpp"
#include "interfaces/mcc128_daq.hpp"
#include "interfaces/servo.hpp"

using namespace std::chrono;

namespace {
const std::string kClientId = "novaGround";
const std::string kCommandTopic = "nova/command";
const std::string kCommandSourceId = "novaOps";
const int kI2CAddr = 0x20;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    TelemetryStore telemetry;

    std::vector<int> daq_hats;
    std::vector<int> daq_channels = {0, 1, 2, 3, 4, 5, 6, 7};
    bool has_daq = false;

    try {
        daq_hats = initialize_daqs();
        std::vector<int> opened_hats;
        for (int hat_id : daq_hats) {
            int result = mcc128_open(hat_id);
            if (result == RESULT_SUCCESS) {
                opened_hats.push_back(hat_id);
            } else {
                std::cerr << "Failed to open DAQ hat " << hat_id
                          << ": code " << result << std::endl;
            }
        }
        daq_hats = std::move(opened_hats);
        has_daq = !daq_hats.empty();
    } catch (const std::exception& e) {
        std::cerr << "DAQ initialization failed: " << e.what() << std::endl;
    }

    Adafruit_PWMServoDriver servo_driver;
    bool has_servo = servo_driver.begin();
    if (has_servo) {
        servo_driver.setPWMFreq(50);
    }

    std::unique_ptr<TCA9535> io_expander =
        std::make_unique<TCA9535>("/dev/i2c-1", kI2CAddr);
    bool has_io_expander = io_expander->is_ready();
    std::bitset<16> relay_state;
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

    std::unique_ptr<GPIO_Manager> gpio_manager;
    bool has_gpio_manager = false;
    try {
        gpio_manager = std::make_unique<GPIO_Manager>("/dev/gpiochip0");
        has_gpio_manager = true;

        std::vector<unsigned int> output_pins = {17, 27, 22};
        std::vector<unsigned int> input_pins = {5, 6};

        for (auto pin : output_pins) {
            gpio_manager->set_direction(pin, "out");
            gpio_manager->write(pin, 0);
        }

        for (auto pin : input_pins) {
            gpio_manager->set_direction(pin, "in");
        }
    } catch (const std::exception& e) {
        std::cerr << "GPIO Manager initialization failed: " << e.what() << std::endl;
        has_gpio_manager = false;
    }

    ServoController servo_controller(has_servo ? &servo_driver : nullptr, &telemetry);
    RelayController relay_controller(has_io_expander ? io_expander.get() : nullptr, &telemetry);
    if (has_io_expander) {
        relay_controller.set_initial_state(relay_state);
    }
    GpioController gpio_controller(has_gpio_manager ? gpio_manager.get() : nullptr, &telemetry);

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

    std::string address = "mqtt://localhost:1883";
    auto cli = std::make_shared<mqtt::async_client>(address, kClientId);

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
                auto connResponse = rsp->get_connect_response();
                connected = true;
                std::cout << "Connected to MQTT broker" << std::endl;

                if (!connResponse.is_session_present()) {
                    auto topics = mqtt::string_collection::create({kCommandTopic});
                    const std::vector<int> qos{1};
                    cli->subscribe(topics, qos);
                }
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

    auto file_writer = DataFileWriter::from_env("NOVA_DATA_FILE");

    std::thread publisher(publisher_loop, cli, std::ref(telemetry), file_writer.get());
    publisher.detach();

    if (has_daq) {
        std::thread sampler(sample_func, daq_hats, daq_channels, std::ref(telemetry));
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

