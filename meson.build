project('Nova Ground', 'cpp', version: '0.0.1')
add_global_arguments(language: 'cpp')
include_directories('/usr/local/include')

# MCC Daq libraries
cpp = meson.get_compiler('cpp')
daqlib = cpp.find_library('daqhats', dirs: '/usr/local/lib', required: false)

#### Grab source files

src = []
subdir('src')

if daqlib.found()
    executable('novaGround', src, dependencies: daqlib)
else
    executable('novaGround', src, dependencies: daqlib)
endif