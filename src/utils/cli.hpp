#pragma once

#include <iosfwd>
#include <string>
#include <vector>

// Settings that come from the command line. The defaults are build-time
// (they differ per target), so the caller supplies them.
struct RuntimeConfig {
    std::string node_id;
    std::string broker;
    std::string backend;
    int verbosity = 1;
    int sample_interval_ms = 1;
    int publish_interval_ms = 50;
};

enum class ParseStatus {
    Run,        // go on and run with `config`
    ExitOk,     // --help was shown; exit 0
    ExitError,  // bad argument; a message was printed; exit 1
};

struct ParseResult {
    ParseStatus status = ParseStatus::Run;
    RuntimeConfig config;
};

bool parse_int(const std::string& text, int& value);
std::string normalize_broker_address(std::string broker);
std::string normalize_backend_base(std::string backend);
std::string build_upload_url(const std::string& backend_base);
void print_usage(std::ostream& out, const char* exe_name);

// `args` excludes the program name. Output goes to `out` (usage) and `err`
// (messages).
ParseResult parse_args(const std::vector<std::string>& args,
                       const RuntimeConfig& defaults,
                       const char* exe_name,
                       std::ostream& out,
                       std::ostream& err);
