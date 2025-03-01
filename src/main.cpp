#include "mqtt/async_client.h"
#include <bitset>
#include <boost/json.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <memory>
#include <random>
#include <string>
#include <thread>

using namespace std;
using namespace std::chrono;

const std::string CLIENT_ID("novaground");
const int I2C_ADDR = 0x40;

struct sensor_datapoint {
    int id;
    double value;
    double time;
};

vector<sensor_datapoint> data;
boost::shared_mutex _data_access;

bitset<16> relay_state;
boost::shared_mutex _relay_state_access;

std::vector<int> initialize_daqs() {
    std::vector<int> connected_daqs;
    int count = hat_list(HAT_ID_ANY, NULL);

    if (count > 0) {
        struct HatInfo* list =
            (struct HatInfo*)malloc(count * sizeof(struct HatInfo));
        hat_list(HAT_ID_ANY, list);

        for (int i = 0; i < count; i++) {
            connected_daqs.push_back(+list[i].address);
            // std::cout<< "Address " << +list[i].address <<std::endl;
            // std::cout<< "Version" << list[i].version<<std::endl;
            // std::cout<< "Product Name" << list[i].product_name<<std::endl;
            // std::cout<< "ID" << list[i].id<<std::endl;
        }

        free(list);
    }

    return connected_daqs;
}

bool pcf8575_write(int fd, bitset<16> bits) {
    uint8_t buf[2];
    uint16_t value = bits.to_ulong();

    buf[0] = value & 0xFF;        // Low byte
    buf[1] = (value >> 8) & 0xFF; // High byte

    if (write(fd, buf, 2) != 2) {
        // i2c transaction failed
        cout << "error writing to pcf8575\n";
        return false;
    }
    return true;
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
void consumer_func(mqtt::async_client_ptr cli, int fd) {
    while (true) {
        auto msg = cli->consume_message();

        if (!msg) {
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
        int index = parsed.at("id").as_int8();
        bool state = parsed.at("is_low").as_bool();

        boost::upgrade_lock<boost::shared_mutex> lock(
            _relay_state_access); // write lock

        relay_state.set(index, state);
        pcf8575_write(fd, relay_state);

        boost::upgrade_lock<boost::shared_mutex> unlock_upgrade(
            _relay_state_access);
    }
}

// send
void publisher_func(mqtt::async_client_ptr cli) {
    while (true) {
        // read lock
        boost::shared_lock<boost::shared_mutex> lock(_data_access);
        boost::json::array json_sensor_data;
        for (auto sd : data) {
            boost::json::object se;
            se["id"] = sd.id;
            se["value"] = sd.value;
            se["timestamp"] = sd.time;
            json_sensor_data.push_back(se);
        }
        // unlock
        boost::shared_lock<boost::shared_mutex> unlock_shared(_data_access);

        boost::shared_lock<boost::shared_mutex> lock(_relay_state_access);
        boost::json::array json_relay_data;
        for (size_t i = 0; i < relay_state.size(); ++i) {
            boost::json::object se;
            se["id"] = i;
            se["state"] = relay_state[i];
            json_relay_data.push_back(se);
        }

        boost::shared_lock<boost::shared_mutex> unlock_shared(
            _relay_state_access);

        boost::json::value payload = {{"sensors", json_sensor_data},
                                      {"actuators", boost::json::array()},
                                      {"relays", json_relay_data}};
        string s_payload = boost::json::serialize(payload);
        cli->publish("novaground/telemetry", s_payload)->wait();

        this_thread::sleep_for(milliseconds(100));
    }
}

// data sampling thread (only samples on address 0 so far)
void sample_func(vector<int> daq_chan) {
    while (true) {
        boost::upgrade_lock<boost::shared_mutex> lock(
            _data_access); // write lock
        data.clear();
        for (auto c : daq_chan) {
            sensor_datapoint sd;
            sd.id = c;
            const auto p1 = std::chrono::system_clock::now();
            sd.value = get_daq_value(0, c);
            sd.time = std::chrono::duration_cast<std::chrono::seconds>(
                          p1.time_since_epoch())
                          .count();
            data.push_back(sd);
        }
        boost::upgrade_lock<boost::shared_mutex> unlock_upgrade(_data_access);

        // sampling frequency
        this_thread::sleep_for(milliseconds(10));
    }
}

int main(int argc, char* argv[]) {
    // open DAQ
    vector<int> daq_chan = {0, 1, 2, 3, 4, 5, 6, 7};
    mcc128_open(0);

    // open I2C
    int fd;
    char filename[20];

    snprintf(filename, 19, "/dev/i2c-1");
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        cout << "failed to open i2c\n";
        exit(1);
    }

    if (ioctl(fd, I2C_SLAVE, I2C_ADDR) < 0) {
        cout << "failed to find i2c addr\n";
        exit(1);
    }

    // mqtt
    string address = "mqtt://localhost:1883";

    // Create an MQTT client using a smart pointer to be shared among threads.
    auto cli = std::make_shared<mqtt::async_client>(address, CLIENT_ID);

    // Connect options for a persistent session and automatic reconnects.
    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(false)
                        .automatic_reconnect(seconds(2), seconds(30))
                        .finalize();

    auto TOPICS = mqtt::string_collection::create({"novaground/command"});
    const vector<int> QOS{1};

    cli->start_consuming();

    auto rsp = cli->connect(connOpts)->get_connect_response();
    cout << "connected to mqtt broker\n" << endl;

    // Subscribe if this is a new session with the server
    if (!rsp.is_session_present()) {
        cli->subscribe(TOPICS, QOS);
    }

    std::thread sample(sample_func, daq_chan);
    std::thread consumer(consumer_func, cli, fd);
    std::thread publisher(publisher_func, cli);

    sample.join();
    publisher.join();
    consumer.join();

    cli->disconnect();
    return 0;
}
