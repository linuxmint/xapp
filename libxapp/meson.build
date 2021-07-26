glib_min_ver = '>=2.44.0'

gio_dep = dependency('gio-2.0', version: glib_min_ver, required: true)
glib_dep = dependency('glib-2.0', version: glib_min_ver, required: true)
gtk3_dep = dependency('gtk+-3.0', version: '>=3.16', required: true)

libdeps = []
libdeps += gio_dep
libdeps += glib_dep
libdeps += gtk3_dep
libdeps += dependency('gdk-pixbuf-2.0', version: '>=2.22.0', required: true)
libdeps += dependency('cairo', required: true)
libdeps += dependency('x11', required: true)

favorite_vfs_sources = [
  'favorite-vfs-file.c',
  'favorite-vfs-file-enumerator.c',
  'favorite-vfs-file-monitor.c'
]

xapp_debug = [
    'xapp-debug.h',
    'xapp-debug.c'
]

xapp_headers = [
    'xapp-favorites.h',
    'xapp-gtk-window.h',
    'xapp-icon-chooser-button.h',
    'xapp-icon-chooser-dialog.h',
    'xapp-monitor-blanker.h',
    'xapp-preferences-window.h',
    'xapp-stack-sidebar.h',
    'xapp-status-icon.h',
    'xapp-status-icon-monitor.h',
    'xapp-style-manager.h',
    'xapp-util.h'
]

xapp_sources = [
    'xapp-favorites.c',
    'xapp-glade-catalog.c',
    'xapp-gtk-window.c',
    'xapp-icon-chooser-button.c',
    'xapp-icon-chooser-dialog.c',
    'xapp-monitor-blanker.c',
    'xapp-preferences-window.c',
    'xapp-stack-sidebar.c',
    'xapp-status-icon.c',
    'xapp-status-icon-monitor.c',
    'xapp-style-manager.c',
    'xapp-util.c'
]

if not app_lib_only
    libdeps += dependency('libgnomekbdui', required: true)
    xapp_headers += 'xapp-kbd-layout-controller.h'
    xapp_sources += 'xapp-kbd-layout-controller.c'
endif

dbus_headers = []

# FIXME: Ugly workaround that simulates the generation of
#        two different targets, so headers can be included
#        explicitly for introspection.
# 
# This can be removed once all platforms use meson >=.46
# and replaced with gnome.gdbus_codegen
xapp_statusicon_interface_sources = custom_target(
    'xapp-statusicon-interface',
    input: 'org.x.StatusIcon.xml',
    output: ['xapp-statusicon-interface.h', 'xapp-statusicon-interface.c'],
    command: [
        codegen,
        'org.x.StatusIcon',
        'xapp-statusicon-interface',
        'StatusIconInterface',
        meson.current_build_dir(),
        '@INPUT@', '@OUTPUT@'
    ]
)

dbus_headers += xapp_statusicon_interface_sources[0]
xapp_sources += xapp_statusicon_interface_sources[1]

# You can't actually access the generated header udring the install_header command below,
# because the command is evaluated prior to the files being generated.  So we need to manually
# install the dbus header file (custom install scripts really *do* get evaluated after build,
# during the install phase.)
codegen = find_program(join_paths(meson.source_root(), 'meson-scripts', 'g-codegen.py'))

meson.add_install_script(join_paths(meson.source_root(), 'meson-scripts', 'install_generated_header.py'),
  'xapp-statusicon-interface.h'
)

xapp_enums = gnome.mkenums('xapp-enums',
    sources : xapp_headers,
    c_template : 'xapp-enums.c.template',
    h_template : 'xapp-enums.h.template',
    identifier_prefix : 'XApp',
    symbol_prefix : 'xapp'
)

libxapp = library('xapp',
    sources  : xapp_headers + xapp_sources + xapp_enums + dbus_headers + favorite_vfs_sources + xapp_debug,
    include_directories: [top_inc],
    version: meson.project_version(),
    soversion: '1',
    dependencies: libdeps,
    c_args: ['-Wno-declaration-after-statement', '-DG_LOG_DOMAIN="XApp"'],
    link_args: [ '-Wl,-Bsymbolic', '-Wl,-z,relro', '-Wl,-z,now', '-lm'],
    install: true
)

install_headers(xapp_headers,
    subdir: 'xapp/libxapp'
)

libxapp_dep = declare_dependency(
    link_with: libxapp,
    include_directories: top_inc,
    dependencies: libdeps,
    sources: [ xapp_headers, dbus_headers ]
)

gir = gnome.generate_gir(libxapp,
    namespace: 'XApp',
    nsversion: '1.0',
    sources: xapp_headers + xapp_sources + dbus_headers + xapp_enums,
    identifier_prefix: 'XApp',
    symbol_prefix: 'xapp_',
    export_packages: 'xapp',
    includes: ['GObject-2.0', 'Gtk-3.0'],
    install: true
)

pkg.generate(
    libraries: libxapp,
    name: 'xapp',
    subdirs: 'xapp',
    description: 'Utility library',
    version: meson.project_version(),
    requires: ['gtk+-3.0', 'gobject-2.0', 'cairo'],
    requires_private: 'xkbfile',
)

install_data(['xapp-glade-catalog.xml'],
    install_dir : join_paths(get_option('datadir'), 'glade/catalogs')
)

gnome.generate_vapi('xapp',
    packages: ['glib-2.0', 'gio-unix-2.0', 'gtk+-3.0'],
    sources: gir[0],
    metadata_dirs: meson.current_source_dir(),
    install: true
)

if not app_lib_only
    gtk3_module = shared_module(
        'xapp-gtk3-module', ['xapp-gtk3-module.c'],
        include_directories: [top_inc],
        dependencies: [gtk3_dep, libxapp_dep],
        install: true,
        install_dir: join_paths(gtk3_dep.get_pkgconfig_variable('libdir'),'gtk-3.0','modules')
    )
endif
