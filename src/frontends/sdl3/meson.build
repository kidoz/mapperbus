sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])

xbrz_sources = files(
    '../../platform/video/xbrz.cpp',
)

sdl3_sources = files(
    'sdl3_video.cpp',
    'sdl3_audio.cpp',
    'sdl3_input.cpp',
    'gpu_upscaler.cpp',
)

mapperbus_sdl3_lib = static_library('mapperbus-sdl3',
    sdl3_sources + xbrz_sources,
    dependencies : [mapperbus_core_dep, sdl3_dep],
)

mapperbus_sdl3_dep = declare_dependency(
    link_with : mapperbus_sdl3_lib,
    dependencies : [mapperbus_core_dep, sdl3_dep],
)
