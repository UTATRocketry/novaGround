#pragma once

#include <functional>
#include <stop_token>

class OutputSupervisor;
class StatsRegistry;

// The output supervisor's thread: every interval_ms, tick() (max on-time and
// comms-loss trips), then `after_tick` (main publishes the config status when
// the trips changed, and pings systemd's watchdog: a stalled loop stops the
// pings). Until a stop is requested. The loop's period is recorded as
// "supervisor" when stats are on. `after_tick` may be empty.
constexpr int kSupervisorIntervalMs = 50;

void supervisor_loop(std::stop_token stop,
                     OutputSupervisor& supervisor,
                     std::function<void()> after_tick,
                     int interval_ms,
                     StatsRegistry* stats);
