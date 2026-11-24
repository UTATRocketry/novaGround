#include "sd_notify.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>

bool sd_notify_send(const std::string& message) {
    const char* path = std::getenv("NOTIFY_SOCKET");
    if (!path || !*path) {
        return false;
    }
    const std::size_t length = std::strlen(path);
    sockaddr_un address{};
    if (length >= sizeof(address.sun_path) || (path[0] != '/' && path[0] != '@')) {
        return false;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path, length);
    if (path[0] == '@') {
        address.sun_path[0] = '\0';  // abstract namespace
    }
    const auto address_length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + length);

    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }
    const ssize_t sent = ::sendto(fd, message.data(), message.size(), MSG_NOSIGNAL,
                                  reinterpret_cast<const sockaddr*>(&address), address_length);
    ::close(fd);
    return sent == static_cast<ssize_t>(message.size());
}
