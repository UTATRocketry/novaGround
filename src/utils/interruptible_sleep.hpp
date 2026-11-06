#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

// A sleep that a stop request cuts short, so a loop can end promptly instead
// of after its next interval. Make one per loop and reuse it: constructing the
// condition variable allocates, waiting on it does not.
class InterruptibleSleep {
public:
    // Sleeps for the duration, or until a stop is requested. Returns true if a
    // stop was requested (so the caller should leave its loop).
    bool sleep_for(std::stop_token stop, std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, stop, duration, [] { return false; });
        return stop.stop_requested();
    }

private:
    std::mutex mutex_;
    std::condition_variable_any cv_;
};
