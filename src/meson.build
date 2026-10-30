subdir('core')
subdir('utils')

# Hardware-free: the chip drivers talk to an I2cBus, the controllers to RelayBank/PwmDriver.
core_src += files(
  'drivers/io_expander.cpp',
  'drivers/servo.cpp',
  'controllers/relay_controller.cpp',
  'controllers/servo_controller.cpp',
)

# Everything below needs the Pi's hardware libraries.
if get_option('hardware')
  subdir('drivers')
  subdir('controllers')
  src += core_src
  src += files('main.cpp')
endif
