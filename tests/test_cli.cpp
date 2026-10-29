#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "utils/cli.hpp"

namespace {
RuntimeConfig defaults() {
    RuntimeConfig d;
    d.node_id = "novaTest";
    d.broker = "localhost:1883";
    d.backend = "http://localhost:8000";
    d.verbosity = 1;
    d.sample_interval_ms = 1;
    d.publish_interval_ms = 50;
    return d;
}

struct Run {
    ParseResult result;
    std::string out;
    std::string err;
};

Run run(const std::vector<std::string>& args) {
    std::ostringstream out, err;
    Run r;
    r.result = parse_args(args, defaults(), "novaTest", out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}
}

BOOST_AUTO_TEST_SUITE(cli)

BOOST_AUTO_TEST_CASE(no_arguments_keeps_the_defaults) {
    auto r = run({});
    BOOST_TEST(r.result.status == ParseStatus::Run);
    BOOST_TEST(r.result.config.node_id == "novaTest");
    BOOST_TEST(r.result.config.broker == "localhost:1883");
    BOOST_TEST(r.result.config.sample_interval_ms == 1);
    BOOST_TEST(r.result.config.publish_interval_ms == 50);
}

BOOST_AUTO_TEST_CASE(every_flag_sets_its_field) {
    auto r = run({"--broker", "10.0.0.2:1884", "--backend", "pc:8001", "--verbosity", "2",
                  "--sample-ms", "100", "--publish-ms", "250"});
    BOOST_TEST(r.result.status == ParseStatus::Run);
    BOOST_TEST(r.result.config.broker == "10.0.0.2:1884");
    BOOST_TEST(r.result.config.backend == "pc:8001");
    BOOST_TEST(r.result.config.verbosity == 2);
    BOOST_TEST(r.result.config.sample_interval_ms == 100);
    BOOST_TEST(r.result.config.publish_interval_ms == 250);
}

BOOST_AUTO_TEST_CASE(later_flags_override_earlier_ones) {
    auto r = run({"--broker", "a", "--broker", "b"});
    BOOST_TEST(r.result.config.broker == "b");
}

BOOST_AUTO_TEST_CASE(help_prints_usage_and_exits_ok) {
    for (const char* flag : {"--help", "-h"}) {
        auto r = run({flag});
        BOOST_TEST(r.result.status == ParseStatus::ExitOk);
        BOOST_TEST(r.out.find("Usage: novaTest") == 0u);
    }
}

BOOST_AUTO_TEST_CASE(unknown_option_is_an_error) {
    auto r = run({"--fas-port", "COM3"});
    BOOST_TEST(r.result.status == ParseStatus::ExitError);
    BOOST_TEST(r.err.find("Unknown option: --fas-port") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(flag_without_a_value_is_an_error) {
    for (const char* flag : {"--broker", "--backend", "--verbosity", "--sample-ms", "--publish-ms"}) {
        auto r = run({flag});
        BOOST_TEST(r.result.status == ParseStatus::ExitError, flag);
    }
}

BOOST_AUTO_TEST_CASE(non_numeric_values_are_errors) {
    for (const char* flag : {"--verbosity", "--sample-ms", "--publish-ms"}) {
        for (const char* bad : {"abc", "", "1x", "1.5", "99999999999"}) {
            auto r = run({flag, bad});
            BOOST_TEST(r.result.status == ParseStatus::ExitError, std::string(flag) + " " + bad);
        }
    }
}

BOOST_AUTO_TEST_CASE(parse_int_rules) {
    int v = 7;
    BOOST_TEST(parse_int("42", v));
    BOOST_TEST(v == 42);
    BOOST_TEST(parse_int("-3", v));
    BOOST_TEST(v == -3);
    v = 7;
    BOOST_TEST(!parse_int("4 2", v));
    BOOST_TEST(v == 7);  // untouched on failure
}

BOOST_AUTO_TEST_CASE(normalize_broker_address_cases) {
    BOOST_TEST(normalize_broker_address("localhost") == "mqtt://localhost:1883");
    BOOST_TEST(normalize_broker_address("10.0.0.2:1884") == "mqtt://10.0.0.2:1884");
    BOOST_TEST(normalize_broker_address("mqtt://host") == "mqtt://host");
    BOOST_TEST(normalize_broker_address("tcp://host:1") == "tcp://host:1");
}

BOOST_AUTO_TEST_CASE(normalize_backend_base_cases) {
    BOOST_TEST(normalize_backend_base("pc") == "http://pc:8000");
    BOOST_TEST(normalize_backend_base("pc:8001") == "http://pc:8001");
    BOOST_TEST(normalize_backend_base("https://pc/") == "https://pc");
    BOOST_TEST(normalize_backend_base("http://pc:8001/") == "http://pc:8001");
}

BOOST_AUTO_TEST_CASE(build_upload_url_cases) {
    BOOST_TEST(build_upload_url("http://pc:8000") == "http://pc:8000/api/data-files/upload");
    BOOST_TEST(build_upload_url("http://pc:8000/api/data-files/upload") ==
               "http://pc:8000/api/data-files/upload");
}

// Every flag the usage text advertises must be accepted.
BOOST_AUTO_TEST_CASE(every_flag_in_the_usage_text_is_accepted) {
    std::ostringstream usage;
    print_usage(usage, "novaTest");
    std::istringstream in(usage.str());
    std::string line;
    int flags = 0;
    while (std::getline(in, line)) {
        auto pos = line.find("--");
        if (pos == std::string::npos) continue;
        std::string flag = line.substr(pos, line.find_first_of(" \t", pos) - pos);
        std::vector<std::string> args{flag};
        if (flag != "--help") args.push_back(flag == "--broker" || flag == "--backend" ? "x" : "1");
        BOOST_TEST(run(args).result.status != ParseStatus::ExitError, flag);
        ++flags;
    }
    BOOST_TEST(flags >= 6);
}

BOOST_AUTO_TEST_SUITE_END()
