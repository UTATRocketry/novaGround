project('Nova Ground', 'cpp', version: '0.0.1', default_options : ['cpp_std=c++20'])
add_global_arguments(language: 'cpp')

# `-Dhardware=false` builds only the hardware-free code (nova_core) and its
# tests, so CI and laptops don't need wiringPi, libgpiod, paho or the MCC HAT
# libraries. The Pi builds with the default (true).
hardware = get_option('hardware')

cpp = meson.get_compiler('cpp')
# clang = cpp.find_clang()
boost_dep = dependency('boost', modules : ['thread','json'])
threads_dep = dependency('threads')

#### Grab source files
src = []
core_src = []

include = []
include += include_directories('src')

subdir('src')

# Hardware-free code: core/ and utils/ need only Boost and the standard library.
nova_core = static_library(
    'nova_core',
    sources: core_src,
    dependencies: [boost_dep, threads_dep],
    include_directories: include,
)

subdir('tests')

if not hardware
    subdir_done()
endif

# MCC Daq libraries
daqlib = cpp.find_library('daqhats', dirs: '/usr/local/lib', required: false)
pahomqttc_lib1 = cpp.find_library('paho-mqttpp3', required: true)
pahomqttc_lib2 = cpp.find_library('paho-mqtt3as', required: true)
libpaho = declare_dependency(dependencies: [pahomqttc_lib1, pahomqttc_lib2])

# WiringPi library. The servo driver no longer uses it (it goes through
# LinuxI2cBus); the dependency stays until during or after the virtual-device
# work so that change can be reversed.
wiringpi_dep = cpp.find_library('wiringPi', required: true)

# libgpiod library
gpiod_dep = dependency('libgpiod', version: '>=1.6', required: true)

# libcurl library (optional)
curl_dep = dependency('libcurl', required: false)

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


deps = [boost_dep,libpaho, wiringpi_dep, gpiod_dep]

if curl_dep.found()
    deps += curl_dep
    add_project_arguments('-DHAVE_LIBCURL', language: 'cpp')
endif

#### Check for library import success
if daqlib.found()
    deps += daqlib
endif

common_cpp_args = [
    '-DNOVA_DEFAULT_BROKER="localhost:1883"',
    '-DNOVA_DEFAULT_BACKEND="http://localhost:8000"',
    '-DNOVA_DEFAULT_VERBOSITY=1',
]

executable(
    'novaGround',
    sources: src,
    dependencies: deps,
    include_directories: include,
    cpp_args: common_cpp_args + [
        '-DNOVA_BUILD_ID="novaGround"',
        '-DNOVA_DEFAULT_SAMPLE_MS=1',
        '-DNOVA_DEFAULT_PUBLISH_MS=50',
    ],
)

executable(
    'novaThermo',
    sources: src,
    dependencies: deps,
    include_directories: include,
    cpp_args: common_cpp_args + [
        '-DNOVA_BUILD_ID="novaThermo"',
        '-DNOVA_DEFAULT_SAMPLE_MS=100',
        '-DNOVA_DEFAULT_PUBLISH_MS=250',
    ],
)

executable(
    'novaMock',
    sources: src,
    dependencies: deps,
    include_directories: include,
    cpp_args: common_cpp_args + [
        '-DNOVA_BUILD_ID="novaMock"',
        '-DNOVA_DEFAULT_SAMPLE_MS=100',
        '-DNOVA_DEFAULT_PUBLISH_MS=250',
        '-DNOVA_MOCK_MODE=1',
    ],
)
