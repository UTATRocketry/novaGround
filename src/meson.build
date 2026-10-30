subdir('core')
subdir('utils')

# Hardware-free: the chip drivers talk to an I2cBus, the controllers and the sampler
# to RelayBank, PwmDriver, DigitalIo and AnalogIo.
core_src += files(
  'drivers/io_expander.cpp',
  'drivers/servo.cpp',
  'controllers/relay_controller.cpp',
  'controllers/servo_controller.cpp',
  'controllers/gpio_controller.cpp',
  'controllers/sampler.cpp',
)

# Everything below needs the Pi's hardware libraries.
if get_option('hardware')
  subdir('drivers')
  subdir('controllers')
  src += core_src
  src += files('main.cpp')
endif
