subdir('core')
subdir('utils')

# Hardware-free: the chip drivers talk to an I2cBus, the controllers and the
# sampler to RelayBank, PwmDriver, DigitalIo and AnalogIo, and the simulator
# (waveforms, scenario files, simulated outputs) needs only Boost.
core_src += files(
  'drivers/io_expander.cpp',
  'drivers/servo.cpp',
  'interfaces/analog_io.cpp',
  'controllers/relay_controller.cpp',
  'controllers/servo_controller.cpp',
  'controllers/gpio_controller.cpp',
  'controllers/sampler.cpp',
  'sim/waveform.cpp',
  'sim/scenario.cpp',
  'sim/sim_actuators.cpp',
)

# The application: needs paho (MQTT) and, for uploads, libcurl, but no Pi hardware.
app_src = files(
  'main.cpp',
  'controllers/data_file_controller.cpp',
  'controllers/loops.cpp',
)

# What only the Pi has: the real chips, libgpiod, MCC's libdaqhats.
real_src = files(
  'drivers/hardware_real.cpp',
  'drivers/linux_gpio.cpp',
  'drivers/linux_i2c_bus.cpp',
  'drivers/mcc_daqhats.cpp',
)

# The simulator's side of the same seams.
sim_app_src = files('sim/hardware_sim.cpp')
sim_daqhats_src = files('sim/daqhats_sim.cpp')
# The real MCC driver, compiled against the simulated daqhats headers.
mcc_driver_src = files('drivers/mcc_daqhats.cpp')
