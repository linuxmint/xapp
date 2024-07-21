project(
  'xapp',
  'c',
version : '2.8.5',
default_options : ['buildtype=debugoptimized'],
meson_version : '>=0.56.0'
)

gnome = import('gnome')
pkg = import('pkgconfig')
i18n = import('i18n')

dbus_services_dir = dependency('dbus-1').get_variable(pkgconfig: 'session_bus_services_dir',
                                                                pkgconfig_define: ['datadir', get_option('datadir')])
sn_watcher_dir = join_paths(get_option('prefix'), get_option('libdir'), 'xapps')

cdata = configuration_data()
cdata.set_quoted('GETTEXT_PACKAGE', 'xapp')
cdata.set_quoted('XAPP_SN_WATCHER_PATH', join_paths([sn_watcher_dir, 'xapp-sn-watcher']))
cdata.set('ENABLE_DEBUG', get_option('buildtype') in ['debug', 'debugoptimized'])

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
