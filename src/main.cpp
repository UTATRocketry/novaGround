#include "mqtt/async_client.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "controllers/data_file_controller.hpp"
#include "controllers/fas_controller.hpp"
#include "controllers/gpio_controller.hpp"
#include "controllers/loops.hpp"
#include "controllers/relay_controller.hpp"
#include "controllers/servo_controller.hpp"
#include "core/command_router.hpp"
#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/fas_link.hpp"
#include "interfaces/fas_serial.hpp"
#include "interfaces/gpio_manager.hpp"
#include "interfaces/io_expander.hpp"
#include "interfaces/mcc_daqhats.hpp"
#include "interfaces/servo.hpp"

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
const std::string kCommandTopic  = "nova/command";
const std::string kConsoleTopic  = "nova/console";
const std::string kCommandSourceId = "novaOps";
const int kI2CAddr = 0x20;

struct RuntimeConfig {
    std::string node_id = NOVA_BUILD_ID;
    std::string broker = NOVA_DEFAULT_BROKER;
    std::string backend = NOVA_DEFAULT_BACKEND;
    int verbosity = NOVA_DEFAULT_VERBOSITY;
    int sample_interval_ms = NOVA_DEFAULT_SAMPLE_MS;
    int publish_interval_ms = NOVA_DEFAULT_PUBLISH_MS;
    // FAS direct serial link — empty string disables FAS integration.
    std::string fas_port;
    int fas_baud = 460800;
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
              << "  --fas-port <device>                 FAS RS-422 serial port (e.g. /dev/ttyUSB0)\n"
              << "  --fas-baud <baud>                   FAS serial baud rate (default 460800)\n"
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
        if (arg == "--fas-port" && i + 1 < argc) {
            config.fas_port = argv[++i];
            continue;
        }
        if (arg == "--fas-baud" && i + 1 < argc) {
            int value = 0;
            if (!parse_int(argv[++i], value)) {
                std::cerr << "Invalid FAS baud value.\n";
                return 1;
            }
            config.fas_baud = value;
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
        if (!config.fas_port.empty()) {
            std::cout << "FAS port: " << config.fas_port
                      << " @ " << config.fas_baud << " baud" << std::endl;
        }
    }

    TelemetryStore telemetry;

    // ---- FAS direct serial link -------------------------------------------
    // Constructed unconditionally; only opened/started if --fas-port is given.
    std::unique_ptr<FasSerial> fas_serial;
    std::unique_ptr<FasLink>   fas_link;
    bool has_fas = false;

    if (!config.fas_port.empty()) {
        fas_serial = std::make_unique<FasSerial>(config.fas_port, config.fas_baud);
        fas_link   = std::make_unique<FasLink>(*fas_serial);

        // Wire FasLink callbacks → TelemetryStore.
        fas_link->on_adc_sample([&telemetry, &data_logger](int board_id,
                                                            const rt_adc_sample_t& s) {
            FasAdcSample sample;
            sample.board_id = board_id;
            sample.t_us     = s.t_us;
            sample.v[0]     = s.ch0 * FasLink::kAdcInt16ToV;
            sample.v[1]     = s.ch1 * FasLink::kAdcInt16ToV;
            sample.mA[0]    = s.ch0 * FasLink::kAdcInt16ToMA;
            sample.mA[1]    = s.ch1 * FasLink::kAdcInt16ToMA;
            telemetry.push_fas_adc(sample);
            data_logger.log_fas_row(board_id, s.t_us,
                                    sample.v[0], sample.v[1],
                                    sample.mA[0], sample.mA[1]);
        });

        fas_link->on_announce([&telemetry](const rt_announce_t& ann) {
            // Build a human-readable board key mirroring the Python GS convention.
            static const char* kKindNames[] = {"GS","FMC","EPB","IMC","RAB","PMB"};
            const char* kind_str = (ann.board_kind < 6)
                ? kKindNames[ann.board_kind] : "UNK";
            FasBoardStatus bs;
            bs.key    = std::string(kind_str) + ":" + std::to_string(ann.board_id);
            bs.online = true;
            telemetry.upsert_fas_board(bs);
        });

        fas_link->on_imc_status([&telemetry](int board_id,
                                             const rt_imc_status_t& st) {
            FasImcStatus imc;
            imc.board_id    = board_id;
            imc.armed       = (st.armed != 0);
            imc.arm_line    = (st.arm_line != 0);
            imc.disarm_line = (st.disarm_line != 0);
            telemetry.set_fas_imc(imc);
        });

        // Route all FasSerial frames through FasLink.
        fas_serial->set_frame_callback(
            [&](uint32_t can_id, const uint8_t* data, size_t len) {
                fas_link->handle_frame(can_id, data, len);
            });

        has_fas = fas_serial->open();
        if (!has_fas) {
            std::cerr << "FAS serial port failed to open; FAS integration disabled.\n";
        }
    }

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
    FasController fas_controller(has_fas ? fas_link.get() : nullptr);

    std::atomic<bool> console_active{false};

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
    router.register_handler("fas", [&fas_controller](const boost::json::object& cmd) {
        fas_controller.handle_command(cmd);
    });
    router.register_handler("console", [&console_active](const boost::json::object& cmd) {
        auto* v = cmd.if_contains("action");
        if (!v || !v->is_string()) return;
        std::string act{v->as_string()};
        if (act == "start")      console_active.store(true,  std::memory_order_relaxed);
        else if (act == "stop")  console_active.store(false, std::memory_order_relaxed);
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

    if (has_fas) {
        // Publish raw FAS frames to nova/console when console mode is active.
        fas_link->on_raw_frame([&](uint32_t can_id,
                                   const uint8_t* data, size_t len) {
            if (!console_active.load(std::memory_order_relaxed)) return;
            if (!cli || !cli->is_connected()) return;

            rt_can_id_t cid = rt_can_id_unpack(can_id);
            std::ostringstream hex;
            hex << std::hex << std::setfill('0');
            for (size_t i = 0; i < len; ++i) {
                if (i) hex << ' ';
                hex << std::setw(2) << static_cast<int>(data[i]);
            }

            boost::json::object frame;
            frame["source"]     = "novaGround";
            frame["type"]       = "fas_frame";
            frame["can_id"]     = can_id;
            frame["msg_type"]   = cid.msg;
            frame["board_kind"] = cid.board_kind;
            frame["board_id"]   = cid.board_id;
            frame["channel"]    = cid.channel;
            frame["data_hex"]   = hex.str();

            try {
                cli->publish(kConsoleTopic,
                             boost::json::serialize(frame))->wait_for(
                    std::chrono::milliseconds(50));
            } catch (...) {}
        });

        // Serial read loop — blocks in read(); must start before discovery.
        std::thread fas_reader([&fas_serial]{ fas_serial->run(); });
        fas_reader.detach();

        // Periodic discovery requests + board timeout checks.
        std::thread fas_disc(fas_discovery_loop,
                             std::ref(*fas_link), std::ref(telemetry));
        fas_disc.detach();

        std::cout << "FAS integration active on " << config.fas_port << std::endl;
    }

    std::thread consumer(consumer_loop, cli, std::ref(router));
    consumer.detach();

    while (true) {
        std::this_thread::sleep_for(seconds(1));
    }
}
