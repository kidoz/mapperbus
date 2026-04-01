catch2_dep = dependency('catch2-with-main', fallback : ['catch2', 'catch2_with_main_dep'])

test_bus = executable('test-bus',
    'unit/test_bus.cpp',
    dependencies : [mapperbus_core_dep, catch2_dep],
)
test('bus', test_bus)

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
