boost_test_dep = dependency('boost', modules : ['unit_test_framework', 'json'])

nova_core_tests = executable(
    'nova_core_tests',
    files(
        'test_main.cpp',
        'test_cli.cpp',
        'test_json_utils.cpp',
        'test_command_router.cpp',
        'test_telemetry.cpp',
        'test_data_logger.cpp',
        'test_data_file.cpp',
        'test_tca9535.cpp',
        'test_relay_controller.cpp',
        'test_pca9685.cpp',
        'test_servo_controller.cpp',
        'test_gpio_controller.cpp',
        'test_sampler.cpp',
        'test_stoppable_loops.cpp',
        'test_waveform.cpp',
        'test_scenario.cpp',
        'test_daqhats_sim.cpp',
        'test_sim_actuators.cpp',
        'test_sim_hardware.cpp',
        '../src/sim/hardware_sim.cpp',
    ),
    link_with: sim_libs,
    dependencies: [boost_test_dep, boost_dep, threads_dep],
    include_directories: [sim_include] + include,
    cpp_args: ['-DBOOST_TEST_DYN_LINK'],
)

test('nova_core', nova_core_tests, timeout: 120)

# Every flag the shell scripts pass to the binary must be one it accepts.
test(
    'scripts_contract',
    find_program('bash'),
    args: [files('scripts_contract.sh'), meson.project_source_root()],
)
