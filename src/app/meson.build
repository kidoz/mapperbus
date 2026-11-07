libarchive_dep = dependency('libarchive',
    version : '>=3.6.0',
    fallback : ['libarchive', 'libarchive_dep'],
    default_options : ['default_library=static', 'werror=false', 'progs=[]',
                       'tests=disabled', 'zlib=enabled', 'lzma=enabled'],
)

app_threads_dep = dependency('threads')
state_storage_platform_deps = []
if host_machine.system() == 'windows'
    state_storage_platform_deps = [cpp.find_library('shell32'), cpp.find_library('ole32')]
endif

mapperbus_app_lib = static_library('mapperbus-app',
    files('app.cpp', 'configuration.cpp', 'emulation_session.cpp', 'session_actions.cpp', 'state_storage.cpp', 'rom_source.cpp', 'async_rom_loader.cpp', 'async_state_io.cpp', 'async_disk_save.cpp', 'state_storage_tasks.cpp', 'live_microphone.cpp'),
    dependencies : [mapperbus_core_dep, libarchive_dep, app_threads_dep] + state_storage_platform_deps,
)

mapperbus_app_dep = declare_dependency(
    link_with : mapperbus_app_lib,
    dependencies : [mapperbus_core_dep, libarchive_dep, app_threads_dep] + state_storage_platform_deps,
    include_directories : mapperbus_inc,
)

if get_option('enable_sdl3')
    executable('mapperbus-sdl3',
        files('main.cpp') + mapperbus_icon_resources,
        dependencies : [mapperbus_sdl3_dep, mapperbus_app_dep],
        link_args : mapperbus_microphone_link_args,
        link_depends : mapperbus_microphone_link_depends,
        install : true,
    )
endif
