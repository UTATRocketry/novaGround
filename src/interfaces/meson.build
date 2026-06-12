src += files(
  'servo.cpp',
  'io_expander.cpp',
  'gpio_manager.cpp',
  'mcc_daqhats.cpp',
  'fas_serial.cpp',
  'fas_link.cpp',
  # fas_proto/rt_proto.c is header-only from the build's perspective:
  # rt_crc16() is reimplemented inline in fas_serial.cpp to avoid mixing
  # C and C++ compilation units in a C++-only meson project.
)
