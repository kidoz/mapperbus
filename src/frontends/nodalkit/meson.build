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
    install : true,
)

# GNOME / FreeDesktop integration assets. The .desktop entry and hicolor icon
# are only meaningful on Linux and match the Wayland app_id ("dev.mapperbus.gui")
# set in main.cpp, so GNOME Shell can group the window and show a launcher icon.
if host_machine.system() == 'linux'
    linux_assets = files('../../../packaging/linux/dev.mapperbus.gui.desktop',
                         '../../../packaging/linux/dev.mapperbus.gui.svg')

    install_data(linux_assets[0],
        install_dir : get_option('datadir') / 'applications',
        rename : 'dev.mapperbus.gui.desktop')

    install_data(linux_assets[1],
        install_dir : get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
        rename : 'dev.mapperbus.gui.svg')
endif

if mapperbus_gui_target_name != 'mapperbus-gui'
    alias_target('mapperbus-gui', mapperbus_gui_exe)
endif
