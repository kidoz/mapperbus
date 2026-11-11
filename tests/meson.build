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

if get_option('enable_cli')
    cli_python = import('python').find_installation()
    test('cli-region', cli_python,
        args : [files('integration/test_cli_region.py'), mapperbus_cli],
        depends : mapperbus_cli,
    )
    test('cli-flash', cli_python,
        args : [files('integration/test_cli_flash.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-jy', cli_python,
        args : [files('integration/test_cli_jy.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-sachen-txc', cli_python,
        args : [files('integration/test_cli_sachen_txc.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-sachen512', cli_python,
        args : [files('integration/test_cli_sachen512.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-sachen513', cli_python,
        args : [files('integration/test_cli_sachen513.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-sachen262', cli_python,
        args : [files('integration/test_cli_sachen262.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-kof97', cli_python,
        args : [files('integration/test_cli_kof97.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-nitra250', cli_python,
        args : [files('integration/test_cli_nitra250.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-kasheng115', cli_python,
        args : [files('integration/test_cli_kasheng115.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-scrambled114', cli_python,
        args : [files('integration/test_cli_scrambled114.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-h2288', cli_python,
        args : [files('integration/test_cli_h2288.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-t9552', cli_python,
        args : [files('integration/test_cli_t9552.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-waixing-mixed-chr', cli_python,
        args : [files('integration/test_cli_waixing_mixed_chr.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-fs303', cli_python,
        args : [files('integration/test_cli_fs303.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-mrcm196', cli_python,
        args : [files('integration/test_cli_mrcm196.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-mapper197', cli_python,
        args : [files('integration/test_cli_mapper197.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-mapper198', cli_python,
        args : [files('integration/test_cli_mapper198.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-fs309', cli_python,
        args : [files('integration/test_cli_fs309.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-datach', cli_python,
        args : [files('integration/test_cli_datach.py'), mapperbus_cli],
        depends : mapperbus_cli,
        timeout : 60,
    )
    test('cli-dcs', cli_python,
        args : [files('integration/test_cli_dcs.py'), mapperbus_cli],
        depends : mapperbus_cli,
    )
endif

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
    'unit/test_mapper_boards.cpp',
    'unit/test_irem.cpp',
    'unit/test_sunsoft_boards.cpp',
    'unit/test_dcs.cpp',
    'unit/test_taito.cpp',
    'unit/test_taito_x1005.cpp',
    'unit/test_taito_x1017.cpp',
    'unit/test_jaleco_ss88006.cpp',
    'unit/test_jaleco_discrete.cpp',
    'unit/test_irem_lrog017.cpp',
    'unit/test_namco175340.cpp',
    'unit/test_mmc3_variants.cpp',
    'unit/test_rambo1.cpp',
    'unit/test_bandai_fcg.cpp',
    'unit/test_datach.cpp',
    'unit/test_flash_boards.cpp',
    'unit/test_jy_company.cpp',
    'unit/test_sachen_txc.cpp',
    'unit/test_sachen512.cpp',
    'unit/test_sachen513.cpp',
    'unit/test_sachen262.cpp',
    'unit/test_kof97.cpp',
    'unit/test_nitra250.cpp',
    'unit/test_kasheng115.cpp',
    'unit/test_scrambled114.cpp',
    'unit/test_h2288.cpp',
    'unit/test_t9552.cpp',
    'unit/test_waixing_mixed_chr.cpp',
    'unit/test_fs303.cpp',
    'unit/test_mrcm196.cpp',
    'unit/test_mapper197.cpp',
    'unit/test_mapper198.cpp',
    'unit/test_fs309.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
# Full-chip snapshot validation is more expensive under ASan/UBSan.
mapper_test_sanitize = get_option('b_sanitize')
mapper_test_timeout = 60
if mapper_test_sanitize.contains('address') or mapper_test_sanitize.contains('undefined')
    mapper_test_timeout = 120
endif
test('mappers', test_mappers, timeout : mapper_test_timeout)

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
test_live_microphone = executable('test-live-microphone',
    'unit/test_live_microphone.cpp', dependencies : [mapperbus_app_dep, catch2_dep])
test('live-microphone', test_live_microphone)

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

test_xbrz = executable('test-xbrz',
    'unit/test_xbrz.cpp',
    '../src/platform/video/xbrz.cpp',
    include_directories : mapperbus_inc,
    dependencies : catch2_dep,
)
test('xbrz', test_xbrz)

if get_option('enable_nodalkit_gui')
    # Compile the production controller without host audio/gamepad adapters.
    # GuiHost supplies a software window/event loop and controlled file pickers.
    test_gui_bios = executable('test-gui-bios',
        'integration/test_gui_bios.cpp',
        mapperbus_gui_controller_sources,
        dependencies : [mapperbus_app_dep, nodalkit_dep, catch2_dep],
        cpp_args : ['-DMAPPERBUS_VERSION="' + meson.project_version() + '"'],
    )
    test('gui-bios', test_gui_bios, timeout : 60)

    test_gui_rom_recovery = executable('test-gui-rom-recovery',
        'integration/test_gui_rom_recovery.cpp',
        dependencies : [mapperbus_app_dep, nodalkit_dep, catch2_dep],
    )
    test('gui-rom-recovery', test_gui_rom_recovery)

    test_gui_close = executable('test-gui-close',
        'integration/test_gui_close.cpp',
        dependencies : [mapperbus_app_dep, nodalkit_dep, catch2_dep],
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

# Public hardware diagnostics are opt-in and hash-checked at runtime.
test_dma_diagnostics = executable('test-dma-diagnostics',
    'integration/test_dma_diagnostics.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('dma-diagnostics', test_dma_diagnostics,
    args : ['--allow-running-no-tests'], timeout : 120)

test_interrupt_diagnostics = executable('test-interrupt-diagnostics',
    'integration/test_interrupt_diagnostics.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('interrupt-diagnostics', test_interrupt_diagnostics,
    args : ['--allow-running-no-tests'], timeout : 120)

test_ppu_diagnostics = executable('test-ppu-diagnostics',
    'integration/test_ppu_diagnostics.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('ppu-diagnostics', test_ppu_diagnostics,
    args : ['--allow-running-no-tests'], timeout : 240)

test_mapper_timing = executable('test-mapper-timing',
    'unit/test_mapper_timing.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('mapper-timing', test_mapper_timing)

if get_option('enable_sdl3')
    test_fsr1 = executable('test-fsr1',
        'unit/test_fsr1.cpp',
        dependencies : [mapperbus_sdl3_dep, catch2_dep],
    )
    test('fsr1', test_fsr1)

    test_sdl3_microphone = executable('test-sdl3-microphone',
        'unit/test_sdl3_microphone.cpp', dependencies : [mapperbus_sdl3_dep, mapperbus_app_dep, catch2_dep])
    test('sdl3-microphone', test_sdl3_microphone, env : ['SDL_AUDIODRIVER=dummy'])
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
test_controller_compatibility = executable('test-controller-compatibility',
    'integration/test_controller_compatibility.cpp',
    dependencies : [mapperbus_app_dep, catch2_dep],
)
test('controller-compatibility', test_controller_compatibility,
    args : ['--allow-running-no-tests'], timeout : 240)

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
    args : ['--allow-running-no-tests'], timeout : 300)

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
