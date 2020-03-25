project('xapp',
    'c',
    version : '1.6.9'
)

gnome = import('gnome')
pkg = import('pkgconfig')
i18n = import('i18n')

dbus_services_dir = dependency('dbus-1').get_pkgconfig_variable('session_bus_services_dir',
                                                                define_variable: ['prefix', get_option('prefix')])

cdata = configuration_data()
cdata.set('GETTEXT_PACKAGE', '"xapp"')

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

c = configure_file(output : 'config.h',
    configuration : cdata
)

top_inc = include_directories('.')

subdir('icons')
subdir('libxapp')
subdir('po')
subdir('pygobject')
subdir('schemas')
subdir('status-applets')
subdir('scripts')
subdir('xapp-sn-watcher')

if get_option('docs')
    subdir('docs')
endif
