/*!
 *  @file Adafruit_PWMServoDriver.h
 *
 *  This is a library for our Adafruit 16-channel PWM & Servo driver.
 *  Re-written for Raspberry Pi.
 *
 *  Designed specifically to work with the Adafruit 16-channel PWM & Servo
 * driver.
 */

#include "servo.hpp"
#include <algorithm>

#define ENABLE_DEBUG_OUTPUT // comment out to suppress debug level dumps

Adafruit_PWMServoDriver::Adafruit_PWMServoDriver(const uint8_t addr)
    : _i2caddr(addr) {}

bool Adafruit_PWMServoDriver::begin(uint8_t prescale) {
    fd = wiringPiI2CSetup(_i2caddr);
    if (fd < 0) {
        std::cerr << "Failed to initialize I2C communication." << std::endl;
        return false;
    }

    std::cout << "I2C communication initialized successfully." << std::endl;
    reset();
    if (prescale) {
        setExtClk(prescale);
    } else {
        setPWMFreq(1000);
    }
    setOscillatorFrequency(FREQUENCY_OSCILLATOR);
    return true;
}

bool Adafruit_PWMServoDriver::is_ready() const {
    return fd >= 0;
}

void Adafruit_PWMServoDriver::reset() {
    if (!ensure_ready("reset")) {
        return;
    }
    write8(PCA9685_MODE1, MODE1_RESTART);
    delay(10);
}

void Adafruit_PWMServoDriver::sleep() {
    if (!ensure_ready("sleep")) {
        return;
    }
    uint8_t awake = read8(PCA9685_MODE1);
    uint8_t sleep = awake | MODE1_SLEEP; // set sleep bit high
    write8(PCA9685_MODE1, sleep);
    delay(5);
}

void Adafruit_PWMServoDriver::wakeup() {
    if (!ensure_ready("wakeup")) {
        return;
    }
    uint8_t sleep = read8(PCA9685_MODE1);
    uint8_t wakeup = sleep & ~MODE1_SLEEP; // set sleep bit low
    write8(PCA9685_MODE1, wakeup);
}

void Adafruit_PWMServoDriver::setExtClk(uint8_t prescale) {
    if (!ensure_ready("setExtClk")) {
        return;
    }
    uint8_t oldmode = read8(PCA9685_MODE1);
    uint8_t newmode = (oldmode & ~MODE1_RESTART) | MODE1_SLEEP; // sleep
    write8(PCA9685_MODE1, newmode);
    write8(PCA9685_MODE1, (newmode |= MODE1_EXTCLK));
    write8(PCA9685_PRESCALE, prescale);

    delay(5);
    write8(PCA9685_MODE1, (newmode & ~MODE1_SLEEP) | MODE1_RESTART | MODE1_AI);

#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << "Mode now 0x" << std::hex << read8(PCA9685_MODE1) << std::dec
              << std::endl;
#endif
}

void Adafruit_PWMServoDriver::setPWMFreq(float freq) {
    if (!ensure_ready("setPWMFreq")) {
        return;
    }
#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << "Attempting to set freq " << freq << std::endl;
#endif
    if (freq < 1) {
        freq = 1;
    }
    if (freq > 3500) {
        freq = 3500;
    }

    float prescaleval = ((_oscillator_freq / (freq * 4096.0)) + 0.5) - 1;
    if (prescaleval < PCA9685_PRESCALE_MIN) {
        prescaleval = PCA9685_PRESCALE_MIN;
    }
    if (prescaleval > PCA9685_PRESCALE_MAX) {
        prescaleval = PCA9685_PRESCALE_MAX;
    }
    uint8_t prescale = static_cast<uint8_t>(prescaleval);

#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << "Final pre-scale: " << prescale << std::endl;
#endif

    uint8_t oldmode = read8(PCA9685_MODE1);
    uint8_t newmode = (oldmode & ~MODE1_RESTART) | MODE1_SLEEP;
    write8(PCA9685_MODE1, newmode);
    write8(PCA9685_PRESCALE, prescale);
    write8(PCA9685_MODE1, oldmode);
    delay(5);
    write8(PCA9685_MODE1, oldmode | MODE1_RESTART | MODE1_AI);

#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << "Mode now 0x" << std::hex << read8(PCA9685_MODE1) << std::dec
              << std::endl;
#endif
}

void Adafruit_PWMServoDriver::setOutputMode(bool totempole) {
    if (!ensure_ready("setOutputMode")) {
        return;
    }
    uint8_t oldmode = read8(PCA9685_MODE2);
    uint8_t newmode = totempole ? (oldmode | MODE2_OUTDRV)
                                : (oldmode & ~MODE2_OUTDRV);
    write8(PCA9685_MODE2, newmode);
#ifdef ENABLE_DEBUG_OUTPUT
    if (totempole) {
        std::cout << "Setting output mode: totempole";
    } else {
        std::cout << "Setting output mode: open drain";
    }
    std::cout << " by setting MODE2 to " << newmode << std::endl;
#endif
}

uint8_t Adafruit_PWMServoDriver::readPrescale(void) {
    if (!ensure_ready("readPrescale")) {
        return PCA9685_PRESCALE_MIN;
    }
    return read8(PCA9685_PRESCALE);
}

uint16_t Adafruit_PWMServoDriver::getPWM(uint8_t num) {
    if (!ensure_ready("getPWM")) {
        return 0;
    }
    uint8_t channel_base_reg = PCA9685_LED0_ON_L + 4 * num;
    uint16_t on = (read8(channel_base_reg + 1) << 8) + read8(channel_base_reg);
    uint16_t off =
        (read8(channel_base_reg + 3) << 8) + read8(channel_base_reg + 2);

    if (off < on) {
        return 4096 + off - on;
    }
    return off - on;
}

void Adafruit_PWMServoDriver::setPWM(uint8_t num, uint16_t on, uint16_t off) {
    if (!ensure_ready("setPWM")) {
        return;
    }
#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << "Setting PWM " << num << ": " << on << "->" << off
              << std::endl;
#endif
    uint8_t channel_base_reg = PCA9685_LED0_ON_L + 4 * num;
    write8(channel_base_reg, static_cast<uint8_t>(on & 0xFF));
    write8(channel_base_reg + 1, on >> 8);
    write8(channel_base_reg + 2, static_cast<uint8_t>(off & 0xFF));
    write8(channel_base_reg + 3, off >> 8);
}

void Adafruit_PWMServoDriver::setPin(uint8_t num, uint16_t val, bool invert) {
    if (!ensure_ready("setPin")) {
        return;
    }
    val = std::min(val, static_cast<uint16_t>(4095));
    if (invert) {
        if (val == 0) {
            setPWM(num, 4096, 0);
        } else if (val == 4095) {
            setPWM(num, 0, 4096);
        } else {
            setPWM(num, 0, 4095 - val);
        }
    } else {
        if (val == 4095) {
            setPWM(num, 4096, 0);
        } else if (val == 0) {
            setPWM(num, 0, 4096);
        } else {
            setPWM(num, 0, val);
        }
    }
}

void Adafruit_PWMServoDriver::writeMicroseconds(uint8_t num,
                                                uint16_t Microseconds) {
    if (!ensure_ready("writeMicroseconds")) {
        return;
    }
#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << "Setting PWM Via Microseconds on output " << num << ": "
              << Microseconds << std::endl;
#endif

    double pulse = Microseconds;
    double pulselength = 1000000;

    uint16_t prescale = readPrescale();

#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << prescale << " PCA9685 chip prescale" << std::endl;
#endif

    prescale += 1;
    pulselength *= prescale;
    pulselength /= _oscillator_freq;

#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << pulselength << " us per bit" << std::endl;
#endif

    pulse /= pulselength;

#ifdef ENABLE_DEBUG_OUTPUT
    std::cout << pulse << " pulse for PWM" << std::endl;
#endif

    setPWM(num, 0, static_cast<uint16_t>(pulse));
}

bool Adafruit_PWMServoDriver::disablePWM(uint8_t num) {
    if (!ensure_ready("disablePWM")) {
        return false;
    }
    setPWM(num, 0, 4096);
    return true;
}

uint32_t Adafruit_PWMServoDriver::getOscillatorFrequency(void) {
    return _oscillator_freq;
}

void Adafruit_PWMServoDriver::setOscillatorFrequency(uint32_t freq) {
    _oscillator_freq = freq;
}

uint8_t Adafruit_PWMServoDriver::read8(uint8_t addr) {
    if (fd < 0) {
        return 0;
    }
    int value = wiringPiI2CReadReg8(fd, addr);
    if (value < 0) {
        return 0;
    }
    return static_cast<uint8_t>(value);
}

void Adafruit_PWMServoDriver::write8(uint8_t addr, uint8_t d) {
    if (fd < 0) {
        return;
    }
    wiringPiI2CWriteReg8(fd, addr, d);
}

void Adafruit_PWMServoDriver::delay(int ms) {
    usleep(ms * MILLI_TO_MICRO);
}

bool Adafruit_PWMServoDriver::ensure_ready(const char* action) {
    if (is_ready()) {
        return true;
    }
    if (!warned_not_ready_) {
        std::cerr << "PWM driver not initialized; " << action << " ignored."
                  << std::endl;
        warned_not_ready_ = true;
    }
    return false;
}

