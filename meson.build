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
core_src = []

include = []
include += include_directories('src')

subdir('src')

# Hardware-free code: core/, utils/, the controllers, the chip drivers (over their
# bus/interface seams) and the simulator need only Boost and the standard library.
nova_core = static_library(
    'nova_core',
    sources: core_src,
    dependencies: [boost_dep, threads_dep],
    include_directories: include,
)

# The simulated libdaqhats (MCC's C API, backed by a scenario file), and the real
# MCC driver compiled against its headers, so the same driver code runs with and
# without the HATs.
sim_include = include_directories('src/sim/include')
daqhats_sim = static_library(
    'daqhats_sim',
    sim_daqhats_src,
    dependencies: [boost_dep, threads_dep],
    include_directories: [sim_include] + include,
    link_with: nova_core,
)
mcc_driver_sim = static_library(
    'mcc_driver_sim',
    mcc_driver_src,
    include_directories: [sim_include] + include,
    link_with: daqhats_sim,
)
sim_libs = [mcc_driver_sim, daqhats_sim, nova_core]

subdir('tests')

# paho (MQTT) and libcurl: needed by the application, not by the library or tests.
pahomqttc_lib1 = cpp.find_library('paho-mqttpp3', required: hardware)
pahomqttc_lib2 = cpp.find_library('paho-mqtt3as', required: hardware)
have_paho = pahomqttc_lib1.found() and pahomqttc_lib2.found()
libpaho = declare_dependency(dependencies: [pahomqttc_lib1, pahomqttc_lib2])

# libcurl library (optional)
curl_dep = dependency('libcurl', required: false)

common_cpp_args = [
    '-DNOVA_DEFAULT_BROKER="localhost:1883"',
    '-DNOVA_DEFAULT_BACKEND="http://localhost:8000"',
    '-DNOVA_DEFAULT_VERBOSITY=1',
]

if curl_dep.found()
    add_project_arguments('-DHAVE_LIBCURL', language: 'cpp')
endif

# novaMock: the real application on simulated hardware. Needs only paho, so it
# builds on a laptop or in CI. See docs/simulation.md.
if have_paho
    mock_deps = [boost_dep, libpaho, threads_dep]
    if curl_dep.found()
        mock_deps += curl_dep
    endif
    novamock = executable(
        'novaMock',
        sources: app_src + sim_app_src,
        dependencies: mock_deps,
        include_directories: include,
        link_with: sim_libs,
        cpp_args: common_cpp_args + [
            '-DNOVA_BUILD_ID="novaMock"',
            '-DNOVA_DEFAULT_SAMPLE_MS=100',
            '-DNOVA_DEFAULT_PUBLISH_MS=250',
        ],
    )

    # End to end on a private broker: telemetry carries the scenario's values and
    # a data-saving command records them. Needs mosquitto and python3.
    smoke_tools = [
        find_program('mosquitto', required: false),
        find_program('mosquitto_sub', required: false),
        find_program('mosquitto_pub', required: false),
        find_program('python3', required: false),
    ]
    smoke_possible = true
    foreach tool : smoke_tools
        smoke_possible = smoke_possible and tool.found()
    endforeach
    if smoke_possible
        test(
            'novamock_smoke',
            find_program('bash'),
            args: [files('tests/novamock_smoke.sh'), novamock],
            suite: 'integration',
            timeout: 90,
        )
    else
        message('mosquitto or python3 not found: not registering the novaMock smoke test')
    endif
else
    message('paho MQTT not found: not building novaMock')
endif

if not hardware
    subdir_done()
endif

# MCC Daq libraries
daqlib = cpp.find_library('daqhats', dirs: '/usr/local/lib', required: false)

# WiringPi library. The servo driver no longer uses it (it goes through
# LinuxI2cBus); the dependency stays until during or after the virtual-device
# work so that change can be reversed.
wiringpi_dep = cpp.find_library('wiringPi', required: true)

# libgpiod library
gpiod_dep = dependency('libgpiod', version: '>=1.6', required: true)

src = core_src + app_src + real_src

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


deps = [boost_dep, libpaho, wiringpi_dep, gpiod_dep, threads_dep]

if curl_dep.found()
    deps += curl_dep
endif

#### Check for library import success
if daqlib.found()
    deps += daqlib
endif

executable(
    'novaGround',
    sources: app_src + real_src,
    dependencies: deps,
    include_directories: include,
    link_with: nova_core,
    cpp_args: common_cpp_args + [
        '-DNOVA_BUILD_ID="novaGround"',
        '-DNOVA_DEFAULT_SAMPLE_MS=1',
        '-DNOVA_DEFAULT_PUBLISH_MS=50',
    ],
)

executable(
    'novaThermo',
    sources: app_src + real_src,
    dependencies: deps,
    include_directories: include,
    link_with: nova_core,
    cpp_args: common_cpp_args + [
        '-DNOVA_BUILD_ID="novaThermo"',
        '-DNOVA_DEFAULT_SAMPLE_MS=100',
        '-DNOVA_DEFAULT_PUBLISH_MS=250',
    ],
)
