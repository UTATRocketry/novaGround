project('Nova Ground', 'cpp', version: '0.0.1', default_options : ['cpp_std=c++20'])
add_global_arguments(language: 'cpp')
include_directories('/usr/local/include')

# MCC Daq libraries
cpp = meson.get_compiler('cpp')
# clang = cpp.find_clang()
daqlib = cpp.find_library('daqhats', dirs: '/usr/local/lib', required: false)

#### Grab source files
src = []
subdir('src')

deps = []

#### Check for library import success
if daqlib.found()
    dependencies += daqlib
endif

executable('novaGround', sources : src, dependencies: deps)