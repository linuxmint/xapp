project('xapp',
    'c',
    version : '2.2.4',
    default_options : [ 'buildtype=debugoptimized' ],
)

gnome = import('gnome')
pkg = import('pkgconfig')
i18n = import('i18n')

dbus_services_dir = dependency('dbus-1').get_pkgconfig_variable('session_bus_services_dir',
                                                                define_variable: ['datadir', get_option('datadir')])
libexec_path = join_paths(get_option('prefix'), get_option('libexecdir'), 'xapps', 'sn-watcher')

cdata = configuration_data()
cdata.set_quoted('GETTEXT_PACKAGE', 'xapp')
cdata.set_quoted('XAPP_SN_WATCHER_PATH', join_paths([libexec_path, 'xapp-sn-watcher']))

add_global_arguments([
    '-Wunused',
    '-Wimplicit-function-declaration'
  ],
  language: 'c'
)

if not get_option('deprecated_warnings')
  add_global_arguments([
      '-Wno-deprecated-declarations',
      '-Wno-deprecated',
      '-Wno-declaration-after-statement',
    ],
    language: 'c',
  )
endif

app_lib_only = get_option('app-lib-only')

c = configure_file(output : 'config.h',
    configuration : cdata
)

top_inc = include_directories('.')
codegen = find_program(join_paths(meson.source_root(), 'meson-scripts', 'g-codegen.py'))

subdir('libxapp')
subdir('po')
subdir('schemas')
subdir('pygobject')

if not app_lib_only
    subdir('icons')
    subdir('status-applets')
    subdir('scripts')
    subdir('data')
endif

if get_option('status-notifier') and not app_lib_only
    subdir('xapp-sn-watcher')
endif

if get_option('docs')
    subdir('docs')
endif
