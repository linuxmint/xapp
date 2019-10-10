glib_min_ver = '>=2.37.3'

libdeps = []
libdeps += dependency('libxfce4panel-2.0', version: '>=4.12.2', required: true)
libdeps += dependency('glib-2.0', version: glib_min_ver, required: true)
libdeps += dependency('gtk+-3.0', version: '>=3.3.16', required: true)
libdeps += libxapp_dep

xfce_plugin_sources = [
    'xappstatusapplet.c',
    'statusicon.c',
]

xappstatusapplet = library('xappstatusapplet',
    sources  : xfce_plugin_sources,
    include_directories: [top_inc],
    version: meson.project_version(),
    soversion: 1,
    dependencies: libdeps,
    c_args: ['-Wno-declaration-after-statement', '-DG_LOG_DOMAIN="XAppStatusIconPlugin"'],
    link_args: [ '-Wl,-Bsymbolic', '-Wl,-z,relro', '-Wl,-z,now', '-lm'],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'xfce4', 'panel', 'plugins')
)

pkg.generate(
    libraries: xappstatusapplet,
    name: 'xappstatusapplet',
    subdirs: 'xapp',
    description: 'xfce plugin',
    version: meson.project_version(),
    requires: ['gtk+-3.0', 'gobject-2.0', 'glib-2.0', 'xapp'],
)

## Desktop file

i18n.merge_file(
  input: 'xappstatusapplet.desktop.in',
  output: 'xappstatusapplet.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'xfce4', 'panel', 'plugins')
)
