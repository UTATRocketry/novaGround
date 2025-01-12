project('Nova Ground', 'cpp', version: '0.0.1', default_options : ['cpp_std=c++20'])
add_global_arguments(language: 'cpp')

# MCC Daq libraries
cpp = meson.get_compiler('cpp')
# clang = cpp.find_clang()
daqlib = cpp.find_library('daqhats', dirs: '/usr/local/lib', required: false)
boost_dep = dependency('boost', modules : ['thread','json'])
    pahomqttc_lib1 = cpp.find_library('paho-mqttpp3', required: true)
    pahomqttc_lib2 = cpp.find_library('paho-mqtt3as', required: true)
    libpaho = declare_dependency(dependencies: [pahomqttc_lib1, pahomqttc_lib2])

#### Grab source files
src = []

include = []

subdir('src')


#### TODO: Set the clang-tidy and clang-format paths as env variables
clangtidy = find_program('clang-tidy', '/usr/local/Cellar/llvm/17.0.6_1/bin/clang-tidy', required: false)
clangformat = find_program('clang-format', '/usr/local/Cellar/llvm/17.0.6_1/bin/clang-format', required: false)



if clangtidy.found()
    message('Running Clang Tidy')
    tidy = run_command([
            clangtidy,
            '--config-file', meson.source_root() + '/.clang-tidy',
            '-p', meson.build_root(),
            '-fix-errors'
    ] + src,
    check: false)
    message(tidy.stderr())
    message('Clang Tidy Complete')
else
    message('Could not find clang-tidy')
endif


if clangformat.found()
    message('Running Clang Format')
    format = run_command([
            clangformat,
            '-style', 'file',
            # '--config-file', meson.source_root() + '/.clang-format',
            '-i'
    ] + src,
    check: false)
    message(format.stderr())
    message('Clang Format Complete')
else
    message('Could not find clang-format')
endif


deps = [boost_dep,libpaho]

#### Check for library import success
if daqlib.found()
    deps += daqlib
endif

executable('novaGround', sources : src, dependencies: deps, include_directories: include)