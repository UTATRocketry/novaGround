#include "cli.hpp"

#include <ostream>

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

void print_usage(std::ostream& out, const char* exe_name) {
    out << "Usage: " << exe_name << " [options]\n"
        << "  --broker <host[:port]|mqtt://...>   MQTT broker address\n"
        << "  --backend <host[:port]|http://...>  Backend base URL\n"
        << "  --verbosity <0|1|2>                 0=quiet,1=info,2=debug\n"
        << "  --sample-ms <ms>                    DAQ sampling interval\n"
        << "  --publish-ms <ms>                   Telemetry publish interval\n"
        << "  --help                              Show this message\n";
}

ParseResult parse_args(const std::vector<std::string>& args,
                       const RuntimeConfig& defaults,
                       const char* exe_name,
                       std::ostream& out,
                       std::ostream& err) {
    ParseResult result;
    result.config = defaults;
    RuntimeConfig& config = result.config;

    auto fail = [&result]() {
        result.status = ParseStatus::ExitError;
        return result;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const bool has_value = i + 1 < args.size();
        if (arg == "--help" || arg == "-h") {
            print_usage(out, exe_name);
            result.status = ParseStatus::ExitOk;
            return result;
        }
        if (arg == "--broker" && has_value) {
            config.broker = args[++i];
            continue;
        }
        if (arg == "--backend" && has_value) {
            config.backend = args[++i];
            continue;
        }
        if (arg == "--verbosity" && has_value) {
            int value = 0;
            if (!parse_int(args[++i], value)) {
                err << "Invalid verbosity value.\n";
                return fail();
            }
            config.verbosity = value;
            continue;
        }
        if (arg == "--sample-ms" && has_value) {
            int value = 0;
            if (!parse_int(args[++i], value)) {
                err << "Invalid sample interval value.\n";
                return fail();
            }
            config.sample_interval_ms = value;
            continue;
        }
        if (arg == "--publish-ms" && has_value) {
            int value = 0;
            if (!parse_int(args[++i], value)) {
                err << "Invalid publish interval value.\n";
                return fail();
            }
            config.publish_interval_ms = value;
            continue;
        }
        err << "Unknown option: " << arg << "\n";
        print_usage(err, exe_name);
        return fail();
    }
    return result;
}
