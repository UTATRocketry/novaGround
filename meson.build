project('Nova Ground', 'cpp', version: '0.0.1')
add_global_arguments(language: 'cpp')
include_directories('/usr/local/include')

# MCC Daq libraries
cpp = meson.get_compiler('cpp')
daqlib = cpp.find_library('daqhats', dirs: '/usr/local/lib')

executable('novaGround', ['main.cpp', 'src/mccdaq.cpp', 'src/mccdaq.h', 'src/dummysensor.cpp', 'src/dummysensor.h'], dependencies: daqlib)
