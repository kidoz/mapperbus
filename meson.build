project('mapperbus', 'cpp',
    version : '0.1.0',
    license : 'MIT',
    default_options : [
        'warning_level=3',
        'werror=true',
        'cpp_std=c++23',
        'b_lto=true',
        'catch2:tests=false',
    ],
)

mapperbus_inc = include_directories('src')

subdir('src/core')
subdir('src/platform')

if get_option('enable_sdl3')
    subdir('src/frontends/sdl3')
endif

subdir('src/app')

if get_option('enable_nodalkit_gui')
    subdir('src/frontends/nodalkit')
endif

if get_option('enable_cli')
    subdir('src/frontends/cli')
endif

if get_option('enable_tests')
    subdir('tests')
endif
