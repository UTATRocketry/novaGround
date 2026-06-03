#include "gpio_manager.hpp"
#include <iostream>

GPIO_Manager::GPIO_Manager(const std::string& chipname) {
    chip_ = gpiod_chip_open(chipname.c_str());
    if (!chip_) {
        throw std::runtime_error("Failed to open GPIO chip: " + chipname);
    }
    else {
        std::cout << "GPIO chip opened successfully: " << chipname << std::endl;
    }
}

GPIO_Manager::~GPIO_Manager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [pin, info] : pins_) {
#ifdef NOVA_LIBGPIOD_V2
        if (info.request) {
            gpiod_line_request_release(info.request);
        }
#else
        if (info.line) {
            gpiod_line_release(info.line);
        }
#endif
    }
    if (chip_) {
        gpiod_chip_close(chip_);
    }
}

bool GPIO_Manager::set_direction(int pin, const std::string& direction) {
    std::lock_guard<std::mutex> lock(mutex_);

#ifdef NOVA_LIBGPIOD_V2
    if (pins_.count(pin) && pins_[pin].request) {
        gpiod_line_request_release(pins_[pin].request);
        pins_.erase(pin);
    }

    auto* settings = gpiod_line_settings_new();
    auto* line_config = gpiod_line_config_new();
    auto* request_config = gpiod_request_config_new();
    if (!settings || !line_config || !request_config) {
        std::cerr << "Failed to allocate GPIO line configuration\n";
        if (settings) {
            gpiod_line_settings_free(settings);
        }
        if (line_config) {
            gpiod_line_config_free(line_config);
        }
        if (request_config) {
            gpiod_request_config_free(request_config);
        }
        return false;
    }

    if (direction == "out") {
        gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
        std::cout << "Set GPIO " << pin << " as output\n";
    } else if (direction == "in") {
        gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
        std::cout << "Set GPIO " << pin << " as input\n";
    } else {
        std::cerr << "Invalid direction for GPIO " << pin << ": " << direction << "\n";
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        return false;
    }

    unsigned int offset = static_cast<unsigned int>(pin);
    gpiod_request_config_set_consumer(request_config, "GpioControl");
    if (gpiod_line_config_add_line_settings(line_config, &offset, 1, settings) < 0) {
        std::cerr << "Failed to configure GPIO " << pin << "\n";
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        return false;
    }

    struct gpiod_line_request* request = gpiod_chip_request_lines(chip_, request_config, line_config);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);

    if (!request) {
        std::cerr << "Failed to request GPIO " << pin << "\n";
        return false;
    }

    pins_[pin] = {request, offset, direction};
    return true;
#else
    if (pins_.count(pin) && pins_[pin].line) {
        gpiod_line_release(pins_[pin].line);
    }

    gpiod_line* line = gpiod_chip_get_line(chip_, pin);
    if (!line) {
        std::cerr << "Failed to get line for GPIO " << pin << "\n";
        return false;
    }

    int result;
    if (direction == "out") {
        result = gpiod_line_request_output(line, "GpioControl", 0);
        std::cout << "Set GPIO " << pin << " as output\n";
    } else if (direction == "in") {
        result = gpiod_line_request_input(line, "GpioControl");
        std::cout << "Set GPIO " << pin << " as input\n";
    } else {
        std::cerr << "Invalid direction for GPIO " << pin << ": " << direction << "\n";
        return false;
    }

    if (result < 0) {
        std::cerr << "Failed to set direction for GPIO " << pin << "\n";
        return false;
    }

    pins_[pin] = {line, direction};
    return true;
#endif
}

bool GPIO_Manager::write(int pin, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pins_.count(pin) || pins_[pin].direction != "out") {
        std::cerr << "Pin " << pin << " not configured as output\n";
        return false;
    }
#ifdef NOVA_LIBGPIOD_V2
    bool success = gpiod_line_request_set_value(
                       pins_[pin].request,
                       pins_[pin].offset,
                       value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) == 0;
#else
    bool success = gpiod_line_set_value(pins_[pin].line, value) == 0;
#endif
    if (!success) {
        std::cerr << "Failed to write to GPIO " << pin << "\n";
        return false;
    }
    std::cout << "Wrote " << value << " to GPIO " << pin << "\n";
    return success;
}

bool GPIO_Manager::read(int pin, int& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pins_.count(pin) || pins_[pin].direction != "in") {
        std::cerr << "Pin " << pin << " not configured as input\n";
        return false;
    }
#ifdef NOVA_LIBGPIOD_V2
    enum gpiod_line_value val = gpiod_line_request_get_value(pins_[pin].request, pins_[pin].offset);
    if (val == GPIOD_LINE_VALUE_ERROR) {
        std::cerr << "Failed to read GPIO " << pin << "\n";
        return false;
    }
    value = (val == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;
#else
    int val = gpiod_line_get_value(pins_[pin].line);
    if (val < 0) {
        std::cerr << "Failed to read GPIO " << pin << "\n";
        return false;
    }
    value = val;
#endif
    std::cout << "Read GPIO " << pin << ": " << value << "\n";
    return true;
}

std::vector<int> GPIO_Manager::get_input_pins() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> result;
    for (const auto& [pin, info] : pins_) {
        if (info.direction == "in") result.push_back(pin);
    }
    return result;
}
std::map<int, int> GPIO_Manager::read_all_inputs() {
    std::map<int, int> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [pin, info] : pins_) {
        if (info.direction == "in") {
#ifdef NOVA_LIBGPIOD_V2
            enum gpiod_line_value val = gpiod_line_request_get_value(info.request, info.offset);
            if (val == GPIOD_LINE_VALUE_ERROR) {
                std::cerr << "Failed to read GPIO " << pin << "\n";
                continue;
            }
            result[pin] = (val == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;
#else
            int val = gpiod_line_get_value(info.line);
            if (val < 0) {
                std::cerr << "Failed to read GPIO " << pin << "\n";
                continue;
            }
            result[pin] = val;
#endif
        }
    }
    return result;
}
