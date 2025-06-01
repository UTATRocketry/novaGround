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

using namespace std;
using namespace std::chrono;
namespace json = boost::json;


const std::string CLIENT_ID("novaground");
const int I2C_ADDR = 0x20;

// ———————— sensor storage —————————
struct sensor_datapoint {
    int hat_id;
    int channel_id;
    double value;
    double time;
};

vector<sensor_datapoint> sensor_data;
boost::shared_mutex _data_access;

// ———————— relay storage ——————————
bitset<16> relay_state;
boost::shared_mutex _relay_state_access;
bool write_failed;

// ———————— servo storage & driver ——————————
struct servo_state {
    int id;
    uint16_t angle;
};
vector<servo_state> servo_data;
boost::shared_mutex _servo_data_access;

Adafruit_PWMServoDriver servoDriver;


void beginServoDriver() {
    servoDriver.begin(); // I2C address and frequency
    servoDriver.setPWMFreq(50); // Set frequency to 50 Hz
}

class TCA9535 {
  private:
    int i2c_fd;
    uint8_t device_address;

    enum Registers {
        INPUT_PORT0 = 0x00,
        INPUT_PORT1 = 0x01,
        OUTPUT_PORT0 = 0x02,
        OUTPUT_PORT1 = 0x03,
        POLARITY_INV0 = 0x04,
        POLARITY_INV1 = 0x05,
        CONFIG_PORT0 = 0x06,
        CONFIG_PORT1 = 0x07
    };

  public:
    TCA9535(const char* i2c_bus, uint8_t address) : device_address(address) {
        // Open I2C bus
        i2c_fd = open(i2c_bus, O_RDWR);
        if (i2c_fd < 0) {
            throw std::runtime_error("Failed to open I2C bus");
        }

        // Set device address
        if (ioctl(i2c_fd, I2C_SLAVE, device_address) < 0) {
            close(i2c_fd);
            throw std::runtime_error("Failed to set I2C device address");
        }
    }

    ~TCA9535() { close(i2c_fd); }

    // Configure port direction (0 = output, 1 = input)
    void configure_port(uint8_t port, uint8_t direction) {
        uint8_t config_reg = (port == 0) ? CONFIG_PORT0 : CONFIG_PORT1;
        write_register(config_reg, direction);
    }

    // Write entire state to output
    void write_output(std::bitset<16> state) {
        bool success = false;
		for (int retries = 0; retries < 10; retries++) {
            write_register(OUTPUT_PORT0, state.to_ulong() & 0xFF);
            write_register(OUTPUT_PORT1, (state.to_ulong() >> 8) & 0xFF);
            std::cout << "Writing Relay States" << std::endl;
            if (write_failed) {
                std::cerr << "Issue with writing relay state. Retrying..." << std::endl;
                usleep(50000);
                continue;
		    }
            success = true;
            break;
        }
        if (!success) throw std::runtime_error("Failed to write relay state after 10 tries. Aborting program...");
    }

    // Read input port
    uint8_t read_input(uint8_t port) {
        uint8_t input_reg = (port == 0) ? INPUT_PORT0 : INPUT_PORT1;
        return read_register(input_reg);
    }

  private:
    void write_register(uint8_t reg, uint8_t value) {
        write_failed = false;
        uint8_t buffer[2] = {reg, value};
        if (write(i2c_fd, buffer, 2) != 2) {
            write_failed = true;
        }
    }

    uint8_t read_register(uint8_t reg) {
        uint8_t value;
        if (write(i2c_fd, &reg, 1) != 1) {
            throw std::runtime_error("Failed to select I2C register");
        }
        if (read(i2c_fd, &value, 1) != 1) {
            throw std::runtime_error("Failed to read from I2C register");
        }
        return value;
    }
};

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
void consumer_func(mqtt::async_client_ptr cli, TCA9535 io_expander) {
    while (true) {
        auto msg = cli->consume_message();
        if (!msg) {
            this_thread::sleep_for(milliseconds(1)); // prevent tight looping
            continue;
        }

        // configure ports as output
        io_expander.configure_port(0, 0x00);
        io_expander.configure_port(1, 0x00);
        
        string m_str = msg->to_string();
        cout << msg->get_topic() << ": " << m_str << endl;

        error_code ec;
        boost::json::value parsed = boost::json::parse(m_str, ec);
        if (ec) {
            std::cout << "Parsing failed: " << ec.message() << "\n";
            continue;
        }
        
        string type  = parsed.as_object().at("type").as_string().c_str();
        if (type == "servo" && parsed.as_object().contains("angle")) {
            int id = parsed.as_object().at("id").as_int64();
            uint16_t angle = static_cast<uint16_t>(parsed.as_object().at("angle").as_int64());
            servoDriver.writeMicroseconds(static_cast<uint8_t>(id), angle);
            /*
            boost::unique_lock<boost::shared_mutex> lock{_servo_data_access};
            for (auto& s : servo_data) {
                if (s.id == id) {
                    s.angle = angle;
                    return;
                }
            }
            servo_data.push_back({id, angle});
            */
        }
        if (type == "relay") {
            int pin = parsed.at("id").as_int64();
            bool state = parsed.at("state").is_int64() ? (parsed.at("state").as_int64() != 0) : parsed.at("state").as_bool();

            if (pin < 0 || pin >= 16) {
                std::cerr << "Invalid pin number: " << pin << std::endl;
                continue;
            } else {
                boost::unique_lock<boost::shared_mutex> lock{_relay_state_access};

                relay_state.set(pin, state);

                io_expander.write_output(relay_state);
            }
        }
    }
}

// ———————— MQTT publisher ——————————
void publisher_func(mqtt::async_client_ptr cli) {
    while (true) {
        boost::json::array json_sensor_data, json_relay_data, json_servo_data;

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
        boost::json::value payload = {{"sensors", json_sensor_data}};
        string s_payload = boost::json::serialize(payload);
        cli->publish("novaground/telemetry", s_payload)->wait();

        this_thread::sleep_for(milliseconds(5));
    }
}

// ———————— DAQ sampling ——————————
// data sampling thread
void sample_func(const std::vector<int>& daq_hats, const std::vector<int>& daq_channels) {
    while (true) {
        std::vector<sensor_datapoint> new_data;
        for (int hat_id : daq_hats) { // Iterate over all DAQ hats
            for (int channel : daq_channels) { // Iterate over all channels for each DAQ hat
                sensor_datapoint sd;
                sd.hat_id = hat_id;
                sd.channel_id = channel;

                const auto now = std::chrono::system_clock::now();
                sd.value = get_daq_value(hat_id, channel); // Read value from the DAQ hat
                sd.time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

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

int main(int argc, char* argv[]) {
    try {
        // open DAQ
        // Detect and initialize DAQ hats
        std::vector<int> daq_hats = initialize_daqs();
        std::vector<int> daq_channels = {0, 1, 2, 3, 4, 5, 6, 7}; // Channels to sample

        for (int hat_id : daq_hats) {
            mcc128_open(hat_id); // Open each DAQ hat
        }
        
        beginServoDriver();
        // open I2C
        TCA9535 io_expander("/dev/i2c-1", I2C_ADDR);

        // configure ports as output
        io_expander.configure_port(0, 0x00);
        io_expander.configure_port(1, 0x00);

        // Set all ports to default states

        for (int i = 0; i < 16; i++) {
            relay_state.set(i, true);
        }
        
	    io_expander.write_output(relay_state);

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

        std::thread sample(sample_func, daq_hats, daq_channels);
        std::thread consumer(consumer_func, cli, io_expander);
        std::thread publisher(publisher_func, cli);

        sample.detach();
        consumer.detach();
        publisher.detach();

        // keep main thread running
        while (true) {
            this_thread::sleep_for(seconds(1));
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }
}
