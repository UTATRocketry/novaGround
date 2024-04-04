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

#### Gotta figure out how to make this work
clangtidy = find_program('clang-tidy', '/usr/local/Cellar/llvm/17.0.6_1/bin/clang-tidy', required: false)
clangformat = find_program('clang-format', '/usr/local/Cellar/llvm/17.0.6_1/bin/clang-format', required: false)

if clangtidy.found()
    run_target(
        'tidy',
        command: [
            clangtidy,
            '--config-file', meson.source_root() + '/.clang-tidy',
            '-p', meson.build_root(),
            '-fix-errors'
        ] + src)
endif


deps = []

#### Check for library import success
if daqlib.found()
    dependencies += daqlib
endif

executable('novaGround', sources : src, dependencies: deps)