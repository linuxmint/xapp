project('xapp',
    'c',
    version : '1.1.0'
)

gnome = import('gnome')
pkg = import('pkgconfig')
i18n = import('i18n')

cdata = configuration_data()
cdata.set('GETTEXT_PACKAGE', '"xapp"')

c = configure_file(output : 'config.h',
    configuration : cdata
)

top_inc = include_directories('.')

subdir('libxapp')
# subdir('po')
subdir('pygobject')
subdir('files')
subdir('schemas')
subdir('docs')
