nodalkit_dep = dependency('nodalkit', fallback : ['nodalkit', 'nk_dep'])
sdl3_audio_dep = dependency('sdl3', required : false, fallback : ['sdl3', 'sdl3_dep'])

mapperbus_gui_sources = files(
    'main.cpp',
    'gui_audio_backend.cpp',
    'gui_controller.cpp',
)

mapperbus_gui_deps = [mapperbus_app_dep, nodalkit_dep]
mapperbus_gui_cpp_args = []

if sdl3_audio_dep.found()
    mapperbus_gui_sources += files('../sdl3/sdl3_audio.cpp')
    mapperbus_gui_deps += sdl3_audio_dep
    mapperbus_gui_cpp_args += ['-DMAPPERBUS_HAVE_SDL3_AUDIO']
endif

executable('mapperbus-gui',
    mapperbus_gui_sources,
    dependencies : mapperbus_gui_deps,
    cpp_args : mapperbus_gui_cpp_args,
)
