subdir('core')
subdir('utils')

# Hardware-free: the relay driver talks to an I2cBus, the controller to a RelayBank.
core_src += files('drivers/io_expander.cpp', 'controllers/relay_controller.cpp')

# Everything below needs the Pi's hardware libraries.
if get_option('hardware')
  subdir('drivers')
  subdir('controllers')
  src += core_src
  src += files('main.cpp')
endif
