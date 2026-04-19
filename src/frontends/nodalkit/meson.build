nodalkit_dep = dependency('nodalkit', fallback : ['nodalkit', 'nk_dep'])
sdl3_audio_dep = dependency('sdl3', required : false, fallback : ['sdl3', 'sdl3_dep'])

mapperbus_gui_sources = files(
    'main.cpp',
    'gui_audio_backend.cpp',
    'gui_controller.cpp',
    '../../platform/video/fsr1.cpp',
    '../../platform/video/xbrz.cpp',
)

mapperbus_gui_deps = [mapperbus_app_dep, nodalkit_dep]
mapperbus_gui_cpp_args = []

if sdl3_audio_dep.found()
    mapperbus_gui_sources += files('../sdl3/sdl3_audio.cpp')
    mapperbus_gui_deps += sdl3_audio_dep
    mapperbus_gui_cpp_args += ['-DMAPPERBUS_HAVE_SDL3_AUDIO', '-DMAPPERBUS_HAVE_SDL3_GAMEPAD']
endif

mapperbus_gui_target_name = 'mapperbus-gui'
if host_machine.system() == 'darwin'
    mapperbus_gui_target_name = 'mapperbus'
endif

mapperbus_gui_exe = executable(mapperbus_gui_target_name,
    mapperbus_gui_sources,
    dependencies : mapperbus_gui_deps,
    cpp_args : mapperbus_gui_cpp_args,
)

if mapperbus_gui_target_name != 'mapperbus-gui'
    alias_target('mapperbus-gui', mapperbus_gui_exe)
endif
