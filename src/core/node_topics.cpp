#include "node_topics.hpp"

#include <iostream>

NodeTopics NodeTopics::for_node(const std::string& node) {
    NodeTopics t;
    t.command_broadcast = "nova/command";
    t.command_node = "nova/command/" + node;
    t.config = "nova/config/" + node;
    t.config_status = t.config + "/status";
    t.heartbeat = "nova/heartbeat/backend";
    return t;
}

std::vector<std::string> NodeTopics::subscriptions() const {
    return {config, heartbeat, command_broadcast, command_node};
}

MessageDispatcher::MessageDispatcher(NodeTopics topics,
                                     CommandRouter& router,
                                     ConfigHandler on_config,
                                     HeartbeatHandler on_heartbeat)
    : topics_(std::move(topics)),
      router_(router),
      on_config_(std::move(on_config)),
      on_heartbeat_(std::move(on_heartbeat)) {}

void MessageDispatcher::dispatch(const std::string& topic,
                                 const std::string& payload,
                                 bool retained,
                                 std::chrono::steady_clock::time_point received) {
    if (retained && (topic == topics_.command_node || topic == topics_.command_broadcast)) {
        // The backend never retains a command. A retained one would be carried
        // out again on every reconnect, long after it was sent.
        std::cerr << "Ignored a retained message on " << topic << ": commands are never retained" << std::endl;
        return;
    }
    if (topic == topics_.command_node) {
        router_.handle_message(payload, CommandOrigin::Node, received);
    } else if (topic == topics_.command_broadcast) {
        router_.handle_message(payload, CommandOrigin::Broadcast, received);
    } else if (topic == topics_.config) {
        if (payload.empty()) {
            // An empty retained message clears the topic on the broker. It is
            // not a config: keep what is applied.
            std::cerr << "Empty message on " << topic << " (retained config cleared); keeping the current config"
                      << std::endl;
            return;
        }
        if (on_config_) on_config_(payload);
    } else if (topic == topics_.heartbeat) {
        if (on_heartbeat_) on_heartbeat_(payload, retained);
    }
}
