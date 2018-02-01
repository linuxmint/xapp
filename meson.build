project('xapp',
    'c',
    version : '1.1.0'
)

gnome = import('gnome')
pkg = import('pkgconfig')
i18n = import('i18n')

build_docs = false
if get_option('enable-doc')
  if find_program('gtkdoc-scan', required: false).found()
    build_docs = true
  else
    warning('gtkdoc not found, disabling documentation')
  endif
endif

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
if build_docs
  subdir('docs')
endif
