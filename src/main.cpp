#include "mqtt/async_client.h"
#include <boost/json.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

using namespace std;
using namespace std::chrono;

const std::string CLIENT_ID("novaground");

struct sensor_datapoint {
    int id;
    double value;
    double time;
};

boost::lockfree::spsc_queue<sensor_datapoint> sensor_buffer{8192};

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
void consumer_func(mqtt::async_client_ptr cli) {
    while (true) {
        auto msg = cli->consume_message();

        if (!msg) {
            continue;
        }

        cout << msg->get_topic() << ": " << msg->to_string() << endl;
    }
}

// send
void publisher_func(mqtt::async_client_ptr cli) {
    while (true) {
        boost::json::array data;
        sensor_datapoint sd;
        while (sensor_buffer.pop(sd)) {
            data.push_back({to_string(sd.id), sd.value, sd.time});
        }
        boost::json::value payload = {{"data", data}};
        string s_payload = boost::json::serialize(payload);
        cli->publish("novaground/telemetry", s_payload)->wait();
        this_thread::sleep_for(milliseconds(1000));
    }
}

// data sampling thread
void sample_func(vector<int> daq_ids) {
    while (true) {
        for (auto id : daq_ids) {
            sensor_datapoint sd;
            sd.id = id;
            const auto p1 = std::chrono::system_clock::now();
            sd.value = get_daq_value(0, id);
            sd.time = std::chrono::duration_cast<std::chrono::seconds>(
                          p1.time_since_epoch())
                          .count();
            sensor_buffer.push(sd);
        }

        // sampling frequency
        this_thread::sleep_for(milliseconds(10));
    }
}

int main(int argc, char* argv[]) {
    vector<int> daq_ids = initialize_daqs();

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

    std::thread sample(sample_func(daq_ids));
    std::thread consumer(consumer_func, cli);
    std::thread publisher(publisher_func, cli);

    sample.join();
    publisher.join();
    consumer.join();

    cli->disconnect();
    return 0;
}
