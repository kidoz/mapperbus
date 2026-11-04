# Generated icons are checked in; normal builds need no image tools.
mapperbus_icon_resources = []
if host_machine.system() == 'windows' and (get_option('enable_sdl3') or get_option('enable_nodalkit_gui'))
    windows = import('windows')
    mapperbus_icon_resources = windows.compile_resources(
        'windows/mapperbus.rc',
        include_directories : include_directories('windows'),
        depend_files : files('windows/mapperbus.ico'),
    )
endif
