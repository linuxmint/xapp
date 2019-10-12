glib_min_ver = '>=2.37.3'

libdeps = []
libdeps += dependency('gio-2.0', version: glib_min_ver, required: true)
libdeps += dependency('glib-2.0', version: glib_min_ver, required: true)
libdeps += dependency('gtk+-3.0', version: '>=3.3.16', required: true)
libdeps += dependency('gdk-pixbuf-2.0', version: '>=2.22.0', required: true)
libdeps += dependency('cairo', required: true)
libdeps += dependency('libgnomekbdui', required: true)
libdeps += dependency('x11', required: true)

xapp_headers = [
    'xapp-gtk-window.h',
    'xapp-icon-chooser-button.h',
    'xapp-icon-chooser-dialog.h',
    'xapp-kbd-layout-controller.h',
    'xapp-monitor-blanker.h',
    'xapp-preferences-window.h',
    'xapp-stack-sidebar.h',
    'xapp-status-icon.h',
    'xapp-status-icon-monitor.h'
]

xapp_sources = [
    'xapp-glade-catalog.c',
    'xapp-gtk-window.c',
    'xapp-icon-chooser-button.c',
    'xapp-icon-chooser-dialog.c',
    'xapp-kbd-layout-controller.c',
    'xapp-monitor-blanker.c',
    'xapp-preferences-window.c',
    'xapp-stack-sidebar.c',
    'xapp-status-icon.c',
    'xapp-status-icon-monitor.c'
]

codegen = find_program('g-codegen.py')

dbus_headers = []

# FIXME: Ugly workaround that simulates the generation of
#        two different targets, so headers can be included
#        explicitly for introspection.
# 
# This can be removed once all platforms use meson >=.46
# and replaced with gnome.gdbus_codegen
generated_sources = custom_target(
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

# You can't actually access the generated header udring the install_header command below,
# because the command is evaluated prior to the files being generated.  So we need to manually
# install the dbus header file (custom install scripts really *do* get evaluated after build,
# during the install phase.)
meson.add_install_script('install_generated_header.py', 'xapp-statusicon-interface.h')

dbus_headers += generated_sources[0]
xapp_sources += generated_sources[1]

xapp_enums = gnome.mkenums('xapp-enums',
    sources : xapp_headers,
    c_template : 'xapp-enums.c.template',
    h_template : 'xapp-enums.h.template',
    identifier_prefix : 'XApp',
    symbol_prefix : 'xapp'
)

libxapp = library('xapp',
    sources  : xapp_headers + xapp_sources + xapp_enums + dbus_headers,
    include_directories: [top_inc],
    version: meson.project_version(),
    soversion: ABI_VERSION,
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
    sources: xapp_headers + xapp_sources + dbus_headers,
    identifier_prefix: 'XApp',
    symbol_prefix: 'xapp_',
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
