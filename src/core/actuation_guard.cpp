#include "actuation_guard.hpp"

#include "stats.hpp"

namespace {
void dropped(StatsRegistry* stats) {
    if (stats) {
        stats->count_stopped_write();
    }
}
}  // namespace

bool StoppedRelayBank::write_output(const std::bitset<16>&) {
    dropped(stats_);
    return true;
}

void StoppedPwmDriver::writeMicroseconds(uint8_t, uint16_t) {
    dropped(stats_);
}

bool StoppedPwmDriver::disablePWM(uint8_t) {
    dropped(stats_);
    return true;
}

bool StoppedDigitalIo::set_direction(int, const std::string&) {
    dropped(stats_);
    return true;
}

bool StoppedDigitalIo::write(int, int) {
    dropped(stats_);
    return true;
}
