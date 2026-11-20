# Prefer build-time SPIR-V compilation for Vulkan. Shadercross is an optional
# system dependency; building its vendored DXC through Meson's CMake bridge
# is unreliable and pulls an entire compiler into a normal emulator build.
sdl3_shadercross_dep = dependency('', required: false)
if host_machine.system() != 'darwin'
    sdl3_shadercross_dep = dependency('sdl3-shadercross', required : false, allow_fallback : false)
endif

glslang = find_program('glslangValidator', required : get_option('vulkan_shaders'))

sdl3_sources = files(
    'sdl3_video.cpp',
    'sdl3_input.cpp',
    'gpu_upscaler.cpp',
    'gpu_fsr_upscaler.cpp',
)

mapperbus_sdl3_deps = [mapperbus_sdl3_audio_dep, mapperbus_upscalers_dep]
mapperbus_sdl3_cpp_args = []

if glslang.found()
    foreach shader : ['xbrz_preprocess', 'xbrz', 'fsr1_easu', 'fsr1_rcas']
        sdl3_sources += custom_target(shader + '-spirv',
            input : 'shaders' / (shader + '.comp'),
            output : shader + '_spirv.hpp',
            command : [glslang, '-V', '-S', 'comp', '--vn', 'k_' + shader + '_spirv',
                       '@INPUT@', '-o', '@OUTPUT@'],
        )
    endforeach
    mapperbus_sdl3_cpp_args += '-DHAVE_VULKAN_SHADERS'
endif

if sdl3_shadercross_dep.found()
    mapperbus_sdl3_deps += sdl3_shadercross_dep
    mapperbus_sdl3_cpp_args += '-DHAVE_SDL_SHADERCROSS'
endif

mapperbus_sdl3_lib = static_library('mapperbus-sdl3-backends',
    sdl3_sources,
    dependencies : mapperbus_sdl3_deps,
    cpp_args: mapperbus_sdl3_cpp_args,
)

summary('Vulkan upscaler shaders', glslang.found(), section : 'Video')
summary('System shadercross', sdl3_shadercross_dep.found(), section : 'Video')

mapperbus_sdl3_dep = declare_dependency(
    link_with : mapperbus_sdl3_lib,
    dependencies : mapperbus_sdl3_deps,
    compile_args: mapperbus_sdl3_cpp_args,
)

executable('mapperbus-sdl3',
    files('main.cpp') + mapperbus_icon_resources,
    dependencies : [mapperbus_sdl3_dep, mapperbus_app_dep],
    link_args : mapperbus_microphone_link_args,
    link_depends : mapperbus_microphone_link_depends,
    install : true,
)
