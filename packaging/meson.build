# Generated icons are checked in; normal builds need no image tools.
mapperbus_icon_resources = []
mapperbus_microphone_link_args = []
mapperbus_microphone_link_depends = []
if host_machine.system() == 'darwin' and (get_option('enable_sdl3') or get_option('enable_nodalkit_gui'))
    # Supply the purpose string for desktop binaries launched outside an .app too.
    microphone_info = files('macos/MicrophoneInfo.plist')
    mapperbus_microphone_link_depends = microphone_info
    mapperbus_microphone_link_args = ['-Xlinker', '-sectcreate', '-Xlinker', '__TEXT',
        '-Xlinker', '__info_plist', '-Xlinker', microphone_info[0].full_path()]
endif
if host_machine.system() == 'windows' and (get_option('enable_sdl3') or get_option('enable_nodalkit_gui'))
    windows = import('windows')
    mapperbus_icon_resources = windows.compile_resources(
        'windows/mapperbus.rc',
        include_directories : include_directories('windows'),
        depend_files : files('windows/mapperbus.ico'),
    )
endif
