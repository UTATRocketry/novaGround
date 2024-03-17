# for unit testing purposes

project('Nova Ground', 'cpp', version: '0.0.1')
add_global_arguments(language: 'cpp')
include_directories('/usr/local/include')

# MCC Daq libraries
cpp = meson.get_compiler('cpp')
daqlib = cpp.find_library('daqhats', dirs: '/usr/local/lib')

executable('testing', ['testing.cpp', 'mccdaq.cpp', 'mccdaq.h', 'dummysensor.cpp', 'dummysensor.h'], dependencies: daqlib)
