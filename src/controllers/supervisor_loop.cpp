#include "supervisor_loop.hpp"

#include <chrono>
#include <exception>
#include <iostream>

#include "core/output_supervisor.hpp"
#include "core/stats.hpp"
#include "utils/interruptible_sleep.hpp"

void supervisor_loop(std::stop_token stop,
                     OutputSupervisor& supervisor,
                     std::function<void()> after_tick,
                     int interval_ms,
                     StatsRegistry* stats) {
    const int interval = interval_ms > 0 ? interval_ms : kSupervisorIntervalMs;
    LoopStats* loop_stats = stats ? stats->loop("supervisor", interval) : nullptr;
    InterruptibleSleep sleeper;
    while (!stop.stop_requested()) {
        if (loop_stats) {
            loop_stats->tick();
        }
        try {
            supervisor.tick();
            if (after_tick) {
                after_tick();
            }
        } catch (const std::exception& e) {
            std::cerr << "Supervisor error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Supervisor error: unknown exception" << std::endl;
        }
        if (sleeper.sleep_for(stop, std::chrono::milliseconds(interval))) {
            break;
        }
    }
}
