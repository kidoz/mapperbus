sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])

# Only try to use SDL_shadercross if not on macOS, where native MSL strings are used directly
sdl3_shadercross_dep = dependency('', required: false)
if host_machine.system() != 'darwin'
    cmake = import('cmake')
    shadercross_opts = cmake.subproject_options()
    shadercross_opts.add_cmake_defines({
      'SDLSHADERCROSS_VENDORED': 'ON',
      'SDLSHADERCROSS_CLI': 'OFF',
      'SDLSHADERCROSS_SHARED': 'OFF',
      'SDLSHADERCROSS_STATIC': 'ON'
    })
    shadercross_sub = cmake.subproject('SDL_shadercross', options: shadercross_opts, required: false)
    if shadercross_sub.found()
        sdl3_shadercross_dep = shadercross_sub.dependency('SDL3_shadercross-static')
    endif
endif

xbrz_sources = files(
    '../../platform/video/xbrz.cpp',
    '../../platform/video/fsr1.cpp',
)

sdl3_sources = files(
    'sdl3_video.cpp',
    'sdl3_audio.cpp',
    'sdl3_input.cpp',
    'gpu_upscaler.cpp',
    'gpu_fsr_upscaler.cpp',
)

mapperbus_sdl3_deps = [mapperbus_core_dep, sdl3_dep]
mapperbus_sdl3_cpp_args = []

if sdl3_shadercross_dep.found()
    mapperbus_sdl3_deps += sdl3_shadercross_dep
    mapperbus_sdl3_cpp_args += '-DHAVE_SDL_SHADERCROSS'
endif

mapperbus_sdl3_lib = static_library('mapperbus-sdl3',
    sdl3_sources + xbrz_sources,
    dependencies : mapperbus_sdl3_deps,
    cpp_args: mapperbus_sdl3_cpp_args,
)

mapperbus_sdl3_dep = declare_dependency(
    link_with : mapperbus_sdl3_lib,
    dependencies : mapperbus_sdl3_deps,
    compile_args: mapperbus_sdl3_cpp_args,
)
