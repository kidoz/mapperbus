sdl3_dep = dependency('sdl3',
    version : '>=3.4.0',
    required : get_option('enable_sdl3'),
    fallback : ['sdl3', 'sdl3_dep'],
)

if sdl3_dep.found()
    sdl3_audio_deps = [sdl3_dep, mapperbus_core_dep]
    if host_machine.system() == 'darwin'
        subdir('macos')
        sdl3_audio_deps += mapperbus_microphone_permission_dep
    endif

    mapperbus_sdl3_audio_lib = static_library('mapperbus-sdl3-audio',
        'sdl3_audio.cpp',
        'sdl3_microphone.cpp',
        dependencies : sdl3_audio_deps,
    )
    mapperbus_sdl3_audio_dep = declare_dependency(
        link_with : mapperbus_sdl3_audio_lib,
        dependencies : sdl3_audio_deps,
    )
endif
