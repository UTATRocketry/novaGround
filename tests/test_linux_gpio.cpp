// LinuxGpio (the real libgpiod driver) against the kernel's gpio-sim module, so
// the real driver is tested somewhere: CI builds with -Dhardware=false and never
// compiles it otherwise.
//
// Needs a gpio-sim chip with at least 28 lines, named by NOVA_GPIO_SIM_CHIP
// (e.g. /dev/gpiochip2). Without it the test is skipped (exit 77, which meson
// reports as SKIP). tests/README.md has the commands that make one (root).
//
// Plain asserts, not Boost.Test, so the skip is an exit code meson understands.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "drivers/linux_gpio.hpp"

namespace {
int failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::cerr << __FILE__ << ":" << __LINE__ << ": FAILED: " #cond << std::endl; \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

std::filesystem::path sim_dir;  // /sys/bus/gpio/devices/gpiochipN

std::string read_attr(int line, const char* attr) {
    std::ifstream in(sim_dir / ("sim_gpio" + std::to_string(line)) / attr);
    std::string value;
    std::getline(in, value);
    return value;
}

void write_attr(int line, const char* attr, const std::string& value) {
    std::ofstream out(sim_dir / ("sim_gpio" + std::to_string(line)) / attr);
    out << value;
}
}  // namespace

int main() {
    const char* chip = std::getenv("NOVA_GPIO_SIM_CHIP");
    if (!chip || !*chip) {
        std::cout << "SKIP: NOVA_GPIO_SIM_CHIP is not set (see tests/README.md)" << std::endl;
        return 77;
    }
    sim_dir = std::filesystem::path("/sys/bus/gpio/devices") / std::filesystem::path(chip).filename();
    if (!std::filesystem::exists(sim_dir / "sim_gpio17")) {
        std::cout << "SKIP: " << sim_dir.string() << " is not a gpio-sim chip with 28 lines" << std::endl;
        return 77;
    }

    {
        LinuxGpio gpio(chip);

        // An output is claimed at the level asked for, from the start.
        CHECK(gpio.configure_output(17, 1));
        CHECK(read_attr(17, "value") == "1");
        CHECK(gpio.write(17, 0));
        CHECK(read_attr(17, "value") == "0");
        // Claiming it again just sets the level.
        CHECK(gpio.configure_output(17, 1));
        CHECK(read_attr(17, "value") == "1");
        CHECK(gpio.read_all_outputs().at(17) == 1);

        // An input reads what the line is pulled to.
        write_attr(5, "pull", "pull-up");
        CHECK(gpio.configure_input(5));
        int level = -1;
        CHECK(gpio.read(5, level));
        CHECK(level == 1);
        write_attr(5, "pull", "pull-down");
        CHECK(gpio.read(5, level));
        CHECK(level == 0);
        CHECK(gpio.read_all_inputs().count(5) == 1);

        // Direction rules: no write to an input, no read of an output.
        CHECK(!gpio.write(5, 1));
        CHECK(!gpio.read(17, level));

        // A line someone else holds can't be claimed, and is left unclaimed.
        LinuxGpio other(chip);
        CHECK(!other.configure_output(17, 0));
        CHECK(!other.write(17, 0));
        CHECK(read_attr(17, "value") == "1");

        // Released, it is free for the next claim.
        CHECK(gpio.write(17, 0));
        CHECK(gpio.release(17));
        CHECK(gpio.read_all_outputs().count(17) == 0);
        CHECK(!gpio.write(17, 1));
        CHECK(other.configure_output(17, 0));
        CHECK(other.release(17));

        // A line the chip doesn't have fails cleanly.
        CHECK(!gpio.configure_output(500, 0));
        CHECK(gpio.read_all_outputs().count(500) == 0);

        // Releasing a pin that isn't claimed is fine.
        CHECK(gpio.release(22));
        CHECK(gpio.release(5));
    }

    if (failures) {
        std::cerr << failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "LinuxGpio against gpio-sim: all checks passed" << std::endl;
    return 0;
}
