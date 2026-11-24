#include <boost/test/unit_test.hpp>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "temp_dir.hpp"
#include "utils/sd_notify.hpp"

namespace {
// Sets (or unsets, for null) NOTIFY_SOCKET for one test.
struct NotifySocketEnv {
    explicit NotifySocketEnv(const char* value) {
        if (value) {
            setenv("NOTIFY_SOCKET", value, 1);
        } else {
            unsetenv("NOTIFY_SOCKET");
        }
    }
    ~NotifySocketEnv() { unsetenv("NOTIFY_SOCKET"); }
};

// A datagram socket bound to `path`, as systemd's would be.
struct Listener {
    int fd = -1;
    explicit Listener(const std::string& path) {
        fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        BOOST_REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        timeval timeout{2, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    ~Listener() { ::close(fd); }
    std::string receive() {
        char buf[256];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string();
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(sd_notify)

BOOST_AUTO_TEST_CASE(without_notify_socket_nothing_is_sent) {
    NotifySocketEnv env(nullptr);
    BOOST_TEST(!sd_notify_send("READY=1"));
    NotifySocketEnv empty("");
    BOOST_TEST(!sd_notify_send("READY=1"));
}

BOOST_AUTO_TEST_CASE(the_message_reaches_the_socket) {
    TempDir dir;
    const std::string path = (dir.path() / "notify").string();
    Listener listener(path);
    NotifySocketEnv env(path.c_str());
    BOOST_TEST(sd_notify_send("READY=1"));
    BOOST_TEST(listener.receive() == "READY=1");
    BOOST_TEST(sd_notify_send("WATCHDOG=1"));
    BOOST_TEST(listener.receive() == "WATCHDOG=1");
}

BOOST_AUTO_TEST_CASE(a_socket_nobody_listens_on_is_a_failure_not_a_crash) {
    TempDir dir;
    const std::string path = (dir.path() / "nobody").string();
    NotifySocketEnv env(path.c_str());
    BOOST_TEST(!sd_notify_send("WATCHDOG=1"));
    NotifySocketEnv relative("relative/path");
    BOOST_TEST(!sd_notify_send("WATCHDOG=1"));
}

BOOST_AUTO_TEST_SUITE_END()
