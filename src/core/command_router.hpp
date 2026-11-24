#pragma once

#include <boost/json.hpp>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>

class StatsRegistry;

// Which topic a command arrived on (docs/design/pi-io-config.md, "Command
// topics"): nova/command reaches every node, nova/command/<node> only this one.
enum class CommandOrigin { Broadcast, Node };

class CommandRouter {
public:
    using Handler = std::function<void(const boost::json::object&)>;

    explicit CommandRouter(std::string expected_source);

    // `node_only`: the command drives this node's outputs (relay, gpio, servo),
    // so it is accepted only from this node's own topic. Arriving on the
    // broadcast topic it is refused and logged: every node would act on it.
    void register_handler(const std::string& type, Handler handler, bool node_only = false);
    // Times every command that reaches a handler, when stats are on. Not owned;
    // must outlive the router. Call before commands arrive.
    void set_stats(StatsRegistry* stats) { stats_ = stats; }

    // `received` is when the message arrived, for the latency statistics.
    void handle_message(const std::string& payload,
                        CommandOrigin origin,
                        std::chrono::steady_clock::time_point received = std::chrono::steady_clock::now());

private:
    struct Entry {
        Handler handler;
        bool node_only = false;
    };

    std::string expected_source_;
    std::unordered_map<std::string, Entry> handlers_;
    StatsRegistry* stats_ = nullptr;

    void handle_command_object(const boost::json::object& cmd,
                               CommandOrigin origin,
                               std::chrono::steady_clock::time_point received);
};
