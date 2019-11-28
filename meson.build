project('xapp',
    'c',
    version : '1.6.4'
)

gnome = import('gnome')
pkg = import('pkgconfig')
i18n = import('i18n')

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

subdir('libxapp')
subdir('po')
subdir('pygobject')
subdir('files')
subdir('schemas')
subdir('status-applets')

if get_option('docs')
    subdir('docs')
endif
