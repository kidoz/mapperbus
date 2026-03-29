core_sources = files(
    'logger.cpp',
    'emulator.cpp',
    'cpu/cpu.cpp',
    'ppu/ppu.cpp',
    'apu/apu.cpp',
    'apu/blip_buffer.cpp',
    'bus/memory_bus.cpp',
    'cartridge/cartridge.cpp',
    'cartridge/ines_header.cpp',
    'cartridge/rom_crc32.cpp',
    'cartridge/rom_database.cpp',
    'mappers/mapper_registry.cpp',
    'mappers/nrom.cpp',
    'mappers/mmc1.cpp',
    'mappers/uxrom.cpp',
    'mappers/cnrom.cpp',
    'mappers/mmc3.cpp',
    'mappers/axrom.cpp',
    'mappers/color_dreams.cpp',
    'mappers/vrc6.cpp',
    'mappers/vrc7.cpp',
    'mappers/mmc5.cpp',
    'mappers/namco163.cpp',
    'mappers/sunsoft5b.cpp',
    'fds/fds.cpp',
    'input/controller.cpp',
)

mapperbus_core_lib = static_library('mapperbus-core',
    core_sources,
    include_directories : mapperbus_inc,
)

mapperbus_core_dep = declare_dependency(
    link_with : mapperbus_core_lib,
    include_directories : mapperbus_inc,
)
