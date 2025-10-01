#include "mqtt/async_client.h"
#include <bitset>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>
#include <fcntl.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <memory>
#include <random>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include "interfaces/servo.hpp"
#include "interfaces/io_expander.hpp"
#include "interfaces/gpio_manager.hpp"

using namespace std;
using namespace std::chrono;
namespace json = boost::json;


const std::string CLIENT_ID("novaground");

// ———————— Global flags ——————————
bool has_daq = false;
bool has_servo = false;
bool has_io_expander = false;
bool has_gpio_manager = false;

// ———————— sensor storage —————————
struct sensor_datapoint {
    int hat_id;
    int channel_id;
    double value;
    double time;
};
vector<sensor_datapoint> sensor_data;
boost::shared_mutex _data_access;

// ———————— I2C IO Expander ——————————
const int I2C_ADDR = 0x20;
std::unique_ptr<TCA9535> io_expander;

// ———————— relay storage ——————————
bitset<16> relay_state;
boost::shared_mutex _relay_state_access;

// ———————— servo driver ——————————
Adafruit_PWMServoDriver servoDriver;

// ———————— servo storage——————————
// const uint8_t I2C_ADDR = 0x40; // Default I2C address for PCA9685
struct servo_state {
    int id;
    uint16_t angle;
};
vector<servo_state> servo_data;
boost::shared_mutex _servo_data_access;

std::unique_ptr<GPIO_Manager> gpio_manager;
boost::shared_mutex _gpio_access;
std::map<int, int> gpio_input_states;


std::vector<int> initialize_daqs() {
    std::vector<int> connected_daqs;
    int count = hat_list(HAT_ID_ANY, NULL);

    if (count < 0) {
        throw std::runtime_error("Error listing DAQ hats");
        return connected_daqs;
    }

    if (count > 0) {
        struct HatInfo* list =
            (struct HatInfo*)malloc(count * sizeof(struct HatInfo));
        hat_list(HAT_ID_ANY, list);

        for (int i = 0; i < count; i++) {
            connected_daqs.push_back(+list[i].address);
            std::cout << "Detected DAQ Hat at address: " << list[i].address << std::endl;
            // std::cout<< "Address " << +list[i].address <<std::endl;
            // std::cout<< "Version" << list[i].version<<std::endl;
            // std::cout<< "Product Name" << list[i].product_name<<std::endl;
            // std::cout<< "ID" << list[i].id<<std::endl;
        }

        free(list);
    }

    return connected_daqs;
}
// int close_daqs(std::vector<int> list_of_daqs) {
//     // NOTE: THIS IS HARDCODED FOR MCC128s, AND WILL NOT, IN THE CURRENT
//     // CONFIGURATION, WORK OTHERWISE. Currently doesn't seem to work.
//     // result_code keeps returning -1
//     for (int i = 0; i < (int)list_of_daqs.capacity(); i++) {
//         std::cout << list_of_daqs[i] << std::endl;
//         int result_code = mcc128_close((uint8_t)list_of_daqs[i]);
//         std::cout << +result_code << std::endl;
//         if (result_code != RESULT_SUCCESS) {
//             std::cout << "ERROR IN close_daqs()" << std::endl;
//             return 0;
//         }
//     }
//     return 1;
// }

// returns daq value, address is board address, channel is analog input
double get_daq_value(int address, int channel) {
    double value;
    int result;
    uint32_t options = OPTS_DEFAULT;
    result = mcc128_a_in_read(address, channel, options, &value);
    return value;
}

// recv
void consumer_func(
        mqtt::async_client_ptr cli, 
        TCA9535& io_expander, 
        Adafruit_PWMServoDriver servoDriver,
        GPIO_Manager& gpio_manager

    ) {
    while (true) {
        auto msg = cli->consume_message();
        if (!msg) {
            this_thread::sleep_for(milliseconds(1)); // prevent tight looping
            continue;
        }
        
        string m_str = msg->to_string();
        cout << msg->get_topic() << ": " << m_str << endl;

        error_code ec;
        boost::json::value parsed = boost::json::parse(m_str, ec);
        if (ec) {
            std::cout << "Parsing failed: " << ec.message() << "\n";
            continue;
        }
        
        string type  = parsed.as_object().at("type").as_string().c_str();

        if (type == "servo" && has_servo && parsed.as_object().contains("angle")) {
            int id = parsed.as_object().at("id").as_int64();
            uint16_t angle = static_cast<uint16_t>(parsed.as_object().at("angle").as_int64());
            servoDriver.writeMicroseconds(static_cast<uint8_t>(id), angle);
        }

        if (type == "relay" && has_io_expander) {
            int pin = parsed.at("id").as_int64();
            bool state = parsed.at("state").is_int64() ?
                         (parsed.at("state").as_int64() != 0) :
                         parsed.at("state").as_bool();

            if (pin >= 0 && pin < 16) {
                try {
                    boost::unique_lock<boost::shared_mutex> lock{_relay_state_access};
                    relay_state.set(pin, state);
                    io_expander.write_output(relay_state);
                } catch (const std::exception& e) {
                    std::cerr << "Error writing to IO Expander: " << e.what() << std::endl;
                }
            }
            else {
                std::cerr << "Invalid pin number: " << pin << std::endl;
                continue;
            }
        }

        if (type == "gpio" && has_gpio_manager && parsed.as_object().contains("id")) {
            int pin = parsed.at("id").as_int64();
        
            boost::unique_lock<boost::shared_mutex> lock{_gpio_access};
            if (parsed.as_object().contains("mode")) {
                std::string mode = parsed.at("mode").as_string().c_str();
                if (mode == "input") {
                    gpio_manager.set_direction(pin, "in");
                } else if (mode == "output") {
                    gpio_manager.set_direction(pin, "out");
                } else {
                    std::cerr << "Invalid GPIO mode: " << mode << std::endl;
                }
            } 
            if (parsed.as_object().contains("state")) {
                // bool state = parsed.at("state").is_int64() ? (parsed.at("state").as_int64() != 0) : parsed.at("state").as_bool();
                int state = parsed.at("state").is_int64() ? 
                            parsed.at("state").as_int64() : 
                            (parsed.at("state").as_bool() ? 1 : 0);
                gpio_manager.write(pin, state);
            }
        }
    }
}

// ———————— MQTT publisher ——————————
void publisher_func(mqtt::async_client_ptr cli) {
    while (true) {
        boost::json::array json_sensor_data, json_gpio_data, json_relay_data, json_servo_data;

        {
            boost::shared_lock<boost::shared_mutex> lock{_data_access};
            for (const auto& sd : sensor_data) {
                boost::json::object se;
                se["hat_id"]   = sd.hat_id;
                se["channel_id"] = sd.channel_id;
                se["value"] = sd.value;
                se["timestamp"] = sd.time;
                json_sensor_data.push_back(se);
            }
        }
        {
            boost::shared_lock<boost::shared_mutex> lock(_gpio_access);
            for (const auto& [pin, state] : gpio_input_states) {
                boost::json::object g;
                g["pin_id"] = pin;
                g["state"] = state;
                json_gpio_data.push_back(g);
            }
        }
        /*
        {
            boost::shared_lock<boost::shared_mutex> lock{_relay_state_access};
            for (size_t i = 0; i < relay_state.size(); ++i) {
                boost::json::object se;
                se["id"] = i;
                se["state"] = relay_state[i];
                json_relay_data.push_back(se);
            }
        }

        {
            boost::shared_lock<boost::shared_mutex> lock{_servo_data_access};
            for (const auto& s : servo_data) {
                boost::json::object se;
                se["id"] = s.id;
                se["angle"] = s.angle;
                json_servo_data.push_back(se);
            }
        }
        */
        boost::json::value payload = {{"sensors", json_sensor_data}
                                      , {"gpios", json_gpio_data}
                                      // , {"relay", json_relay_data}
                                      // , {"servo", json_servo_data}
                                      };
        string s_payload = boost::json::serialize(payload);
        cli->publish("novaground/telemetry", s_payload)->wait();

        this_thread::sleep_for(milliseconds(5));
    }
}

// ———————— DAQ sampling ——————————
// data sampling thread
void sample_func(const std::vector<int>& daq_hats, const std::vector<int>& daq_channels) {
    if (!has_daq) return;
    while (true) {
        std::vector<sensor_datapoint> new_data;
        for (int hat_id : daq_hats) { // Iterate over all DAQ hats
            for (int channel : daq_channels) { // Iterate over all channels for each DAQ hat
                sensor_datapoint sd;
                sd.hat_id = hat_id;
                sd.channel_id = channel;

                const auto now = std::chrono::system_clock::now();
                sd.value = get_daq_value(hat_id, channel); // Read value from the DAQ hat
                sd.time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

                new_data.push_back(sd);
            }
        }
        {
            boost::unique_lock<boost::shared_mutex> lock(_data_access);
            sensor_data =
                std::move(new_data); // replace with new data atomically
        }

        // sampling frequency
        this_thread::sleep_for(milliseconds(1));
    }
}

void gpio_sampler_func() {
    while (true) {
        std::map<int, int> input_vals;
        {
            boost::unique_lock<boost::shared_mutex> lock(_gpio_access);
            input_vals = gpio_manager->read_all_inputs();
            gpio_input_states = input_vals;
        }
        std::this_thread::sleep_for(milliseconds(50)); // adjust frequency as needed
    }
}

int main(int argc, char* argv[]) {
    // Detect and initialize DAQ hats
    std::vector<int> daq_hats;
    std::vector<int> daq_channels = {0, 1, 2, 3, 4, 5, 6, 7}; // Channels to sample

    try {
        daq_hats = initialize_daqs();
        for (int hat_id : daq_hats) {
            mcc128_open(hat_id); // Open each DAQ hat
        }
        has_daq = !daq_hats.empty();
    } catch (const std::exception& e) {
        std::cerr << "DAQ initialization failed: " << e.what() << std::endl;
    }
    
    try {
        // Initialize servo driver
        servoDriver.begin(); // I2C address and frequency
        servoDriver.setPWMFreq(50); // Set frequency to 50 Hz
        has_servo = true;
    } catch (const std::exception& e) {
        std::cerr << "Servo driver initialization failed: " << e.what() << std::endl;
        has_servo = false;
    }

    try {
        // open I2C
        io_expander = std::make_unique<TCA9535>("/dev/i2c-1", I2C_ADDR);
        has_io_expander = true;
        // configure ports as output
        io_expander->configure_port(0, 0x00);
        io_expander->configure_port(1, 0x00);

        // Set all ports to default states
        for (int i = 0; i < 16; i++) relay_state.set(i, true);
        io_expander->write_output(relay_state);
    } catch (const std::exception& e) {
        std::cerr << "TCA9535 initialization failed: " << e.what() << std::endl;
        has_servo = false;
    }
    try {
        // Initialize GPIO pins
        gpio_manager = std::make_unique<GPIO_Manager>("/dev/gpiochip0");
        has_gpio_manager = true;

        // --- Configure default GPIO directions ---
        std::vector<unsigned int> output_pins = {17, 27, 22};
        std::vector<unsigned int> input_pins  = {5, 6};


        for (auto pin : output_pins) {
            gpio_manager->set_direction(pin, "out");
            gpio_manager->write(pin, 0); // Set initial state to low
        }

        for (auto pin : input_pins) {
            gpio_manager->set_direction(pin, "in"); 
        }

    } catch (const std::exception& e) {
        std::cerr << "GPIO Manager initialization failed: " << e.what() << std::endl;
        has_gpio_manager = false;
    }
    try {
        // mqtt
        string address = "mqtt://localhost:1883";

        // Create an MQTT client using a smart pointer to be shared among
        // threads.
        auto cli = std::make_shared<mqtt::async_client>(address, CLIENT_ID);

        // Connect options for a persistent session and automatic reconnects.
        auto connOpts = mqtt::connect_options_builder()
                            .clean_session(false)
                            .automatic_reconnect(seconds(1), seconds(10))
                            .finalize();

        auto TOPICS = mqtt::string_collection::create({"novaground/command"});
        const vector<int> QOS{1};

        cli->start_consuming();

        auto rsp = cli->connect(connOpts);
        if (!rsp) {
            std::cerr << "Failed to connect to MQTT broker" << std::endl;
            return -1;
        }

        auto connResponse = rsp->get_connect_response();
        cout << "Connected to MQTT broker" << endl;

        if (!connResponse.is_session_present()) {
            cli->subscribe(TOPICS, QOS);
        }

        std::thread publisher(publisher_func, cli);
        publisher.detach();

        if (has_daq) {
            std::thread sample(sample_func, daq_hats, daq_channels);
            sample.detach();
        }
        if (has_gpio_manager) {
            std::thread gpio_sampler(gpio_sampler_func);
            gpio_sampler.detach();
        }

        if (has_io_expander || has_servo || has_gpio_manager) {
            std::thread consumer(consumer_func, cli, std::ref(*io_expander), servoDriver, std::ref(*gpio_manager));
            consumer.detach();
        }
        else {
            std::cerr << "No Actuators initialized. Exiting..." << std::endl;
            return -1;
        }
        

        // keep main thread alive
        while (true) std::this_thread::sleep_for(seconds(1));

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }
}
