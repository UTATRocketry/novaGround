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
        'test_stats.cpp',
        'test_actuation_guard.cpp',
        'test_actuator_state.cpp',
        'test_waveform.cpp',
        'test_scenario.cpp',
        'test_daqhats_sim.cpp',
        'test_sim_actuators.cpp',
        'test_sim_hardware.cpp',
        'test_pi_io_config.cpp',
        'test_output_supervisor.cpp',
        'test_io_config_controller.cpp',
        'test_node_topics.cpp',
        'test_sd_notify.cpp',
        'test_hardware_check.cpp',
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

# LinuxGpio, the real GPIO driver, against the kernel's gpio-sim module. Built
# wherever libgpiod v1 is installed (it never is in CI); it skips itself unless
# NOVA_GPIO_SIM_CHIP names a gpio-sim chip. tests/README.md has the setup.
gpiod_test_dep = dependency('libgpiod', version: ['>=1.6', '<2'], required: false)
if gpiod_test_dep.found()
    linux_gpio_tests = executable(
        'linux_gpio_tests',
        files('test_linux_gpio.cpp', '../src/drivers/linux_gpio.cpp'),
        dependencies: [gpiod_test_dep],
        include_directories: include,
    )
    # -Dgpio_sim_chip=/dev/gpiochipN, or NOVA_GPIO_SIM_CHIP in the environment.
    gpio_sim_chip = get_option('gpio_sim_chip')
    gpio_sim_env = gpio_sim_chip != '' ? ['NOVA_GPIO_SIM_CHIP=' + gpio_sim_chip] : []
    test('linux_gpio', linux_gpio_tests, suite: 'hardware', env: gpio_sim_env)
endif
