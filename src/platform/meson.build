if get_option('enable_sdl3') or get_option('enable_nodalkit_gui') or get_option('enable_tests')
    upscaler_threads_dep = dependency('threads')
    mapperbus_upscalers_lib = static_library('mapperbus-upscalers',
        'video/xbrz.cpp',
        'video/fsr1.cpp',
        include_directories : mapperbus_inc,
        dependencies : upscaler_threads_dep,
    )
    mapperbus_upscalers_dep = declare_dependency(
        link_with : mapperbus_upscalers_lib,
        dependencies : upscaler_threads_dep,
        include_directories : mapperbus_inc,
    )
endif
