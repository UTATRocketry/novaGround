#pragma once

#include <string>

// systemd's notification protocol, without libsystemd: one datagram to the
// socket named by $NOTIFY_SOCKET (a path, or "@name" for the abstract
// namespace). novaGround@.service runs with Type=notify and WatchdogSec, so it
// sends "READY=1" once startup is done and "WATCHDOG=1" from the output
// supervisor's tick.
//
// Does nothing and returns false when NOTIFY_SOCKET is unset or empty (novaMock,
// run.sh, the tests). Never throws; a failed send returns false.
bool sd_notify_send(const std::string& message);
