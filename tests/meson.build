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
    ),
    link_with: nova_core,
    dependencies: [boost_test_dep, boost_dep, threads_dep],
    include_directories: include,
    cpp_args: ['-DBOOST_TEST_DYN_LINK'],
)

test('nova_core', nova_core_tests)

# Every flag the shell scripts pass to the binary must be one it accepts.
test(
    'scripts_contract',
    find_program('bash'),
    args: [files('scripts_contract.sh'), meson.project_source_root()],
)
