add_languages('objcpp', native : false)
microphone_frameworks = dependency('appleframeworks', modules : ['AVFoundation', 'Foundation'])
microphone_permission_lib = static_library('mapperbus-microphone-permission',
    'microphone_permission.mm',
    include_directories : mapperbus_inc,
    dependencies : microphone_frameworks,
    objcpp_args : ['-fobjc-arc'],
    override_options : ['objcpp_std=c++23'],
)
mapperbus_microphone_permission_dep = declare_dependency(
    link_with : microphone_permission_lib,
    dependencies : microphone_frameworks,
)
