catch2_dep = dependency('catch2-with-main', fallback : ['catch2', 'catch2_with_main_dep'])

# Catch2 expands __COUNTER__ in user translation units. Clang 22 diagnoses
# this longstanding extension under -Wpedantic even in C++23 mode.
if cpp.get_id() == 'clang'
    catch2_dep = declare_dependency(
        dependencies : catch2_dep,
        compile_args : cpp.get_supported_arguments('-Wno-c2y-extensions'),
    )
endif

test_bus = executable('test-bus',
    'unit/test_bus.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('bus', test_bus)

test_cpu = executable('test-cpu',
    'unit/test_cpu.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('cpu', test_cpu)

test_ppu = executable('test-ppu',
    'unit/test_ppu.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('ppu', test_ppu)

test_nrom = executable('test-nrom',
    'unit/test_nrom.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('nrom', test_nrom)

test_region = executable('test-region',
    'unit/test_region.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('region', test_region)

test_rom_database = executable('test-rom-database',
    'unit/test_rom_database.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('rom-database', test_rom_database)

test_apu = executable('test-apu',
    'unit/test_apu.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('apu', test_apu)

test_mappers = executable('test-mappers',
    'unit/test_mappers.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('mappers', test_mappers)

test_battle_city_render = executable('test-battle-city-render',
    'integration/test_battle_city_render.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('battle-city-render', test_battle_city_render, workdir : meson.project_source_root())

test_ring_buffer = executable('test-ring-buffer',
    'unit/test_ring_buffer.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('ring-buffer', test_ring_buffer)

test_save_state = executable('test-save-state',
    'unit/test_save_state.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('save-state', test_save_state)

test_battery_save = executable('test-battery-save',
    'unit/test_battery_save.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('battery-save', test_battery_save)

test_fds = executable('test-fds',
    'unit/test_fds.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('fds', test_fds)

test_fds_system = executable('test-fds-system',
    'unit/test_fds_system.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('fds-system', test_fds_system)

test_mmc5_vrc7 = executable('test-mmc5-vrc7',
    'unit/test_mmc5_vrc7.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('mmc5-vrc7', test_mmc5_vrc7)

test_audio_pipeline = executable('test-audio-pipeline',
    'unit/test_audio_pipeline.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('audio-pipeline', test_audio_pipeline)

test_emulation_session = executable('test-emulation-session',
    'unit/test_emulation_session.cpp',
    dependencies : [mapperbus_app_dep, catch2_dep],
)
test('emulation-session', test_emulation_session, workdir : meson.project_source_root())

test_configuration = executable('test-configuration',
    'unit/test_configuration.cpp',
    dependencies : [mapperbus_app_dep, catch2_dep],
)
test('configuration', test_configuration)

test_rom_source = executable('test-rom-source',
    'unit/test_rom_source.cpp',
    dependencies : [mapperbus_app_dep, catch2_dep],
)
test('rom-source', test_rom_source)

test_state_storage = executable('test-state-storage',
    'unit/test_state_storage.cpp',
    dependencies : [mapperbus_app_dep, catch2_dep],
)
test('state-storage', test_state_storage)


if get_option('enable_nodalkit_gui')
    test_gui_close = executable('test-gui-close',
        'integration/test_gui_close.cpp',
        dependencies : [mapperbus_core_dep, nodalkit_dep, catch2_dep],
    )
    test('gui-close', test_gui_close)

    test_input_binding_table = executable('test-input-binding-table',
        'unit/test_input_binding_table.cpp',
        include_directories : mapperbus_inc,
        dependencies : [nodalkit_dep, catch2_dep],
    )
    test('input-binding-table', test_input_binding_table)

    if sdl3_audio_dep.found()
        test_nodalkit_input = executable('test-nodalkit-input',
            'unit/test_nodalkit_input.cpp',
            dependencies : [mapperbus_app_dep, nodalkit_dep, sdl3_audio_dep, catch2_dep],
            cpp_args : ['-DMAPPERBUS_HAVE_SDL3_GAMEPAD'],
        )
        test('nodalkit-input', test_nodalkit_input)
    endif
endif

test_cpu_accuracy = executable('test-cpu-accuracy',
    'unit/test_cpu_accuracy.cpp',
    dependencies : [mapperbus_app_dep, catch2_dep],
)
test('cpu-accuracy', test_cpu_accuracy, workdir : meson.project_source_root())

test_mapper_timing = executable('test-mapper-timing',
    'unit/test_mapper_timing.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('mapper-timing', test_mapper_timing)

if get_option('enable_sdl3')
    test_sdl3_input = executable('test-sdl3-input',
        'unit/test_sdl3_input.cpp',
        dependencies : [mapperbus_sdl3_dep, mapperbus_app_dep, catch2_dep],
    )
    test('sdl3-input', test_sdl3_input)

    test_sdl3_gpu = executable('test-sdl3-gpu',
        'integration/test_sdl3_gpu.cpp',
        dependencies : [mapperbus_sdl3_dep, catch2_dep],
    )
    test('sdl3-gpu', test_sdl3_gpu)
endif

# Licensed BIOS/game assets are opt-in; ordinary CI reports these cases skipped.
test_fds_compatibility = executable('test-fds-compatibility',
    'integration/test_fds_compatibility.cpp',
    dependencies : [mapperbus_app_dep, catch2_dep],
)
test('fds-compatibility', test_fds_compatibility,
    args : ['--allow-running-no-tests'], timeout : 720)

test_fds_diagnostics = executable('test-fds-diagnostics',
    'integration/test_fds_diagnostics.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('fds-diagnostics', test_fds_diagnostics,
    args : ['--allow-running-no-tests'], timeout : 120)

# Original read-only capture ROM; generated without a BIOS or external assembler.
probe_python = import('python').find_installation()
fds_audio_probe = custom_target('fds-audio-probe',
    input : '../tools/build_fds_audio_probe.py',
    output : ['fds-audio-probe.fds', 'fds-audio-probe.json'],
    command : [probe_python, '@INPUT@', '--output', '@OUTPUT0@', '--manifest', '@OUTPUT1@'],
)
test_fds_audio_probe = executable('test-fds-audio-probe',
    'integration/test_fds_audio_probe.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
    cpp_args : ['-DMAPPERBUS_FDS_AUDIO_PROBE_PATH="' + (meson.current_build_dir() / 'fds-audio-probe.fds') + '"'],
)
test('fds-audio-probe', test_fds_audio_probe, depends : fds_audio_probe, timeout : 120)
