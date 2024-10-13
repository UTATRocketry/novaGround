//#include "interfaces/ActuatorInterface/ActuatorManager/DummyActuatorManager.h"
//#include "interfaces/SensorInterface/SensorManager/DummySensorManager.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include <async_mqtt5.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>

constexpr auto use_nothrow_awaitable =
    boost::asio::as_tuple(boost::asio::use_awaitable);

int next_sensor_reading() {
    srand(static_cast<unsigned int>(std::time(0)));
    return rand() % 100;
}

using client_type =
    async_mqtt5::mqtt_client<boost::asio::ip::tcp::socket>; // use tcp

boost::asio::awaitable<bool> subscribe(client_type& client) {
    // Configure the request to subscribe to a Topic.
    async_mqtt5::subscribe_topic sub_topic = async_mqtt5::subscribe_topic{
        "b_cmd",
        async_mqtt5::subscribe_options{
            async_mqtt5::qos_e::exactly_once, // All messages will arrive at
                                              // QoS 2.
            async_mqtt5::no_local_e::no, // Forward message from Clients with
                                         // same ID.
            async_mqtt5::retain_as_published_e::retain, // Keep the original
                                                        // RETAIN flag.
            async_mqtt5::retain_handling_e::send // Send retained messages when
                                                 // the subscription is
                                                 // established.
        }};

    // Subscribe to a single Topic.
    auto&& [ec, sub_codes, sub_props] = co_await client.async_subscribe(
        sub_topic, async_mqtt5::subscribe_props{}, use_nothrow_awaitable);

    if (ec)
        std::cout << "Subscribe error occurred: " << ec.message() << std::endl;

    co_return !ec &&
        !sub_codes[0]; // True if the subscription was successfully established.
}

boost::asio::awaitable<void> subscribe_and_receive(client_type& client) {
    client.brokers("localhost", 1883).async_run(boost::asio::detached);

    if (!(co_await subscribe(client)))
        co_return;

    for (;;) {
        // Receive an Appplication Message from the subscribed Topic(s).
        auto&& [ec, topic, payload, publish_props] =
            co_await client.async_receive(use_nothrow_awaitable);

        if (ec == async_mqtt5::client::error::session_expired) {
            // The Client has reconnected, and the prior session has expired.
            // As a result, any previous subscriptions have been lost and must
            // be reinstated.
            if (co_await subscribe(client))
                continue;
            else
                break;
        } else if (ec)
            break;

        std::cout << "Received message from the Broker" << std::endl;
        std::cout << "\t topic: " << topic << std::endl;
        std::cout << "\t payload: " << payload << std::endl;
        // todo actual implementation
    }

    co_return;
}

boost::asio::awaitable<void>
publish(client_type& client, boost::asio::steady_timer& timer) {
    client.brokers("localhost", 1883).async_run(boost::asio::detached);

    for (;;) {
        // Get the next sensor reading.
        auto reading = std::to_string(next_sensor_reading());

        // Publish the sensor reading with QoS 1.
        auto&& [ec, rc, props] =
            co_await client.async_publish<async_mqtt5::qos_e::at_least_once>(
                "b_cmd", reading, async_mqtt5::retain_e::no,
                async_mqtt5::publish_props{}, use_nothrow_awaitable);

        if (ec) {
            std::cout << "Publish error occurred: " << ec.message()
                      << std::endl;
            break;
        }

        if (!rc)
            std::cout << "Published sensor reading: " << reading << std::endl;

        timer.expires_after(std::chrono::milliseconds(1000));
        auto&& [tec] = co_await timer.async_wait(use_nothrow_awaitable);

        //timer error
        if (tec)
            break;
    }

    co_return;
}

int main() {
    // context
    boost::asio::io_context ioc;
    client_type client(ioc);

    // global timer (system)
    boost::asio::steady_timer timer(ioc);

    // signals for stop
    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

    signals.async_wait([&ioc](async_mqtt5::error_code, int /* signal */) {
        // After we are done with publishing all the messages, cancel the
        // timer and the client. Alternatively, use
        // mqtt_client::async_disconnect.
        ioc.stop();
    });

    // recv
    co_spawn(ioc.get_executor(), subscribe_and_receive(client), boost::asio::detached);
    // send
    co_spawn(ioc.get_executor(), publish(client, timer),
             boost::asio::detached);

    // Start the execution.
    ioc.run();
    return 0;
}

// auto main() -> int {
//     std::vector<std::tuple<int, std::string, double, double>> sensorParams{
//         {0, "Sensor One", 0, 10},
//         {1, "Sensor Two", 10, 20},
//         {2, "Sensor Three", 20, 30},
//     };

//     DummySensorManager dummySensorMan = DummySensorManager(sensorParams);
//     std::cout << std::get<0>(dummySensorMan.querySensors()[0]);

//     int i{0};
//     while (i++ < 1000) {
//         for (std::tuple<int, double>& sensorState :
//              dummySensorMan.querySensors()) {
//             std::cout << "ID:" << std::get<0>(sensorState) << "\n";
//             std::cout << "Sensor Value" << std::get<1>(sensorState) << "\n";
//             std::cout << "\n"
//                       << "\n";
//         }
//     }
// }
