#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "core/command_router.hpp"

// The MQTT topics a node listens on, and which part of the program each goes
// to (docs/design/pi-io-config.md):
//
//   nova/command                   commands for every node (data_file, console)
//   nova/command/<node>            commands for this node (relay, gpio, servo)
//   nova/config/<node>             the retained per-node I/O config
//   nova/config/<node>/status      what this node applied (published, retained)
//   nova/heartbeat/backend         the backend's heartbeat and last will
//
// <node> is the node the commands and config are addressed to: the build's own
// name, or for novaMock the node it stands in for (NOVA_STANDS_IN_FOR).
struct NodeTopics {
    std::string command_broadcast;
    std::string command_node;
    std::string config;
    std::string config_status;
    std::string heartbeat;

    static NodeTopics for_node(const std::string& node);
    // What to subscribe to, in this order: config first, so a retained config
    // is applied before a retained command could be.
    std::vector<std::string> subscriptions() const;
};

// Sends each incoming message to the part of the program its topic belongs to.
// Messages on any other topic are ignored.
class MessageDispatcher {
public:
    using ConfigHandler = std::function<void(const std::string& payload)>;
    using HeartbeatHandler = std::function<void(const std::string& payload, bool retained)>;

    MessageDispatcher(NodeTopics topics,
                      CommandRouter& router,
                      ConfigHandler on_config,
                      HeartbeatHandler on_heartbeat);

    void dispatch(const std::string& topic,
                  const std::string& payload,
                  bool retained,
                  std::chrono::steady_clock::time_point received = std::chrono::steady_clock::now());

    const NodeTopics& topics() const { return topics_; }

private:
    NodeTopics topics_;
    CommandRouter& router_;
    ConfigHandler on_config_;
    HeartbeatHandler on_heartbeat_;
};
