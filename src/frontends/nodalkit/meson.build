nodalkit_dep = dependency('nodalkit', version : '>=0.3.4', fallback : ['nodalkit', 'nk_dep'])
sdl3_audio_dep = dependency('sdl3', required : false, fallback : ['sdl3', 'sdl3_dep'])

mapperbus_gui_sources = files(
    'main.cpp',
    'gui_audio_backend.cpp',
    'gui_controller.cpp',
    '../../platform/video/fsr1.cpp',
    '../../platform/video/xbrz.cpp',
)

mapperbus_gui_deps = [mapperbus_app_dep, nodalkit_dep]
if host_machine.system() == 'windows'
    mapperbus_gui_deps += [cpp.find_library('user32'), cpp.find_library('shell32')]
endif

mapperbus_gui_cpp_args = ['-DMAPPERBUS_VERSION="' + meson.project_version() + '"']

if sdl3_audio_dep.found()
    mapperbus_gui_sources += files('../sdl3/sdl3_audio.cpp', '../sdl3/sdl3_microphone.cpp')
    mapperbus_gui_deps += [sdl3_audio_dep, mapperbus_microphone_permission_dep]
    mapperbus_gui_cpp_args += ['-DMAPPERBUS_HAVE_SDL3_AUDIO', '-DMAPPERBUS_HAVE_SDL3_GAMEPAD']
endif

mapperbus_gui_target_name = 'mapperbus-gui'
mapperbus_gui_install_rpath = ''
if host_machine.system() == 'darwin'
    mapperbus_gui_target_name = 'mapperbus'
elif host_machine.system() == 'linux' and nodalkit_dep.type_name() == 'internal'
    fs = import('fs')
    # The bundled toolkit must also resolve with custom prefixes and on distros
    # whose loader does not search /usr/local/lib by default.
    toolkit_libdir = fs.relative_to(get_option('prefix') / get_option('libdir'),
                                   get_option('prefix') / get_option('bindir'))
    mapperbus_gui_install_rpath = '$ORIGIN' / toolkit_libdir
endif

mapperbus_gui_exe = executable(mapperbus_gui_target_name,
    mapperbus_gui_sources + mapperbus_icon_resources,
    dependencies : mapperbus_gui_deps,
    cpp_args : mapperbus_gui_cpp_args,
    install : true,
    install_rpath : mapperbus_gui_install_rpath,
    link_args : mapperbus_microphone_link_args,
    link_depends : mapperbus_microphone_link_depends,
)

# GNOME / FreeDesktop integration assets. The .desktop entry and hicolor icon
# are only meaningful on Linux and match the Wayland app_id ("dev.mapperbus.gui")
# set in main.cpp, so GNOME Shell can group the window and show a launcher icon.
if host_machine.system() == 'linux'
    linux_assets = files('../../../packaging/linux/dev.mapperbus.gui.desktop',
                         '../../../packaging/linux/dev.mapperbus.gui.svg')

    install_data(linux_assets[0],
        install_dir : get_option('datadir') / 'applications',
        install_tag : 'runtime',
        rename : 'dev.mapperbus.gui.desktop')

    install_data(linux_assets[1],
        install_dir : get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
        install_tag : 'runtime',
        rename : 'dev.mapperbus.gui.svg')

    foreach icon_size : ['16', '24', '32', '48', '64', '128', '256', '512']
        install_data('../../../assets/branding/png/mapperbus-' + icon_size + '.png',
            install_dir : get_option('datadir') / 'icons' / 'hicolor' / (icon_size + 'x' + icon_size) / 'apps',
            install_tag : 'runtime',
            rename : 'dev.mapperbus.gui.png')
    endforeach
endif

if mapperbus_gui_target_name != 'mapperbus-gui'
    alias_target('mapperbus-gui', mapperbus_gui_exe)
endif

# Build a Finder/Dock application with the same identity as the Linux launcher.
if host_machine.system() == 'darwin'
    bundle_python = import('python').find_installation()
    bundle_config = configuration_data()
    bundle_config.set('VERSION', meson.project_version())
    bundle_plist = configure_file(
        input : '../../../packaging/macos/Info.plist.in',
        output : 'Info.plist',
        configuration : bundle_config,
    )
    run_target('mapperbus-macos-app',
        command : [bundle_python, files('../../../tools/package_macos.py'),
                   '--executable', mapperbus_gui_exe,
                   '--icon', files('../../../packaging/macos/MapperBus.icns'),
                   '--plist', bundle_plist,
                   '--output', meson.project_build_root() / 'MapperBus.app'],
    )
endif
