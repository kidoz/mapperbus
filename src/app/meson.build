mapperbus_app_lib = static_library('mapperbus-app',
    files('app.cpp', 'emulation_session.cpp', 'session_actions.cpp'),
    dependencies : [mapperbus_core_dep],
)

mapperbus_app_dep = declare_dependency(
    link_with : mapperbus_app_lib,
    dependencies : [mapperbus_core_dep],
    include_directories : mapperbus_inc,
)

if get_option('enable_sdl3')
    executable('mapperbus-sdl3',
        files('main.cpp'),
        dependencies : [mapperbus_sdl3_dep, mapperbus_app_dep],
    )
endif
