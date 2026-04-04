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

cpp = meson.get_compiler('cpp')

# === Hardening + extra warnings ===
# These are added only if the compiler accepts them, so we stay portable
# across Clang (Apple + upstream), GCC, and future toolchains.
hardening_candidates = [
    # Extra diagnostics
    '-Wshadow',
    '-Wcast-align',
    '-Wformat=2',
    '-Wformat-security',
    '-Wnon-virtual-dtor',
    '-Woverloaded-virtual',
    '-Wdouble-promotion',
    '-Wundef',
    '-Wmissing-declarations',
    # Runtime hardening
    '-fstack-protector-strong',
    '-fno-common',
]

# _FORTIFY_SOURCE requires optimisation; only enable in non-debug builds.
if get_option('buildtype') != 'debug'
    hardening_candidates += '-D_FORTIFY_SOURCE=2'
endif

add_project_arguments(
    cpp.get_supported_arguments(hardening_candidates),
    language : 'cpp',
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
