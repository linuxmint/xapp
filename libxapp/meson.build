#----------------- Common build items ------------------#


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

dbus_headers += generated_sources[0]

install_data(['xapp-glade-catalog.xml'],
    install_dir : join_paths(get_option('datadir'), 'glade/catalogs')
)

glib_min_ver = '>=2.37.3'

libdeps_common = []
libdeps_common += dependency('gio-2.0', version: glib_min_ver, required: true)
libdeps_common += dependency('glib-2.0', version: glib_min_ver, required: true)


#-----------------  GTK3 (libxapp) ---------------------#


libdeps_gtk3 = libdeps_common
libdeps_gtk3 += dependency('gdk-pixbuf-2.0', version: '>=2.22.0', required: true)
libdeps_gtk3 += dependency('cairo', required: true)
libdeps_gtk3 += dependency('x11', required: true)
libdeps_gtk3 += dependency('libgnomekbdui', required: true)
libdeps_gtk3 += dependency('gtk+-3.0', version: '>=3.3.16', required: true)

xapp_gtk3_headers = [
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

xapp_gtk3_sources = [
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

xapp_gtk3_sources += generated_sources[1]

xapp_gtk3_enums = gnome.mkenums('xapp-enums',
    sources : xapp_gtk3_headers,
    c_template : 'xapp-enums.c.template',
    h_template : 'xapp-enums.h.template',
    identifier_prefix : 'XApp',
    symbol_prefix : 'xapp'
)

libxapp_gtk3 = library('xapp',
    sources  : xapp_gtk3_headers + xapp_gtk3_sources + xapp_gtk3_enums + dbus_headers,
    include_directories: [top_inc],
    version: meson.project_version(),
    soversion: '1',
    dependencies: libdeps_gtk3,
    c_args: ['-Wno-declaration-after-statement', '-DG_LOG_DOMAIN="XApp"'],
    link_args: [ '-Wl,-Bsymbolic', '-Wl,-z,relro', '-Wl,-z,now', '-lm'],
    install: true
)

install_headers(xapp_gtk3_headers,
    subdir: 'xapp/libxapp'
)

# This is for doc generation, I don't think it's important for the gtk2 build.
libxapp_gtk3_dep = declare_dependency(
    link_with: libxapp_gtk3,
    include_directories: top_inc,
    dependencies: libdeps_gtk3,
    sources: [ xapp_gtk3_headers, dbus_headers ]
)

gir_gtk3 = gnome.generate_gir(libxapp_gtk3,
    namespace: 'XApp',
    nsversion: '1.0',
    sources: xapp_gtk3_headers + xapp_gtk3_sources + dbus_headers,
    identifier_prefix: 'XApp',
    symbol_prefix: 'xapp_',
    includes: ['GObject-2.0', 'Gtk-3.0'],
    install: true
)

pkg.generate(
    libraries: libxapp_gtk3,
    name: 'xapp',
    subdirs: 'xapp',
    description: 'Utility library',
    version: meson.project_version(),
    requires: ['gtk+-3.0', 'glib-2.0', 'gio-2.0', 'gobject-2.0', 'cairo'],
    requires_private: 'xkbfile',
)

gnome.generate_vapi('xapp',
    packages: ['glib-2.0', 'gio-unix-2.0', 'gtk+-3.0'],
    sources: gir_gtk3[0],
    metadata_dirs: meson.current_source_dir(),
    install: true
)


#-----------------  GTK2 (libxapp-status-icon-gtk2) ----#


libdeps_gtk2 = libdeps_common
libdeps_gtk2 += dependency('gio-2.0', version: glib_min_ver, required: true)
libdeps_gtk2 += dependency('glib-2.0', version: glib_min_ver, required: true)
libdeps_gtk2 += dependency('gtk+-2.0', version: '>=2.24.0', required: true)

xapp_gtk2_headers = [
    'xapp-status-icon.h',
    'xapp-status-icon-monitor.h'
]

xapp_gtk2_sources = [
    'xapp-status-icon.c',
    'xapp-status-icon-monitor.c'
]

xapp_gtk2_sources += generated_sources[1]

libxapp_gtk2 = library('xapp-status-icon-gtk2',
    sources  : xapp_gtk2_headers + xapp_gtk2_sources + dbus_headers,
    include_directories: [top_inc],
    version: meson.project_version(),
    soversion: '1',
    dependencies: libdeps_gtk2,
    c_args: ['-Wno-declaration-after-statement', '-DG_LOG_DOMAIN="XApp"'],
    link_args: [ '-Wl,-Bsymbolic', '-Wl,-z,relro', '-Wl,-z,now', '-lm'],
    install: true
)

install_headers(xapp_gtk2_headers,
    subdir: 'xapp-status-icon-gtk2/libxapp'
)

gir_gtk2 = gnome.generate_gir(libxapp_gtk2,
    namespace: 'XAppGtk2',
    nsversion: '1.0',
    sources: xapp_gtk2_headers + xapp_gtk2_sources + dbus_headers,
    identifier_prefix: 'XApp',
    symbol_prefix: 'xapp_',
    includes: ['GObject-2.0', 'Gtk-2.0'],
    install: true
)

pkg.generate(
    libraries: libxapp_gtk2,
    name: 'xapp-status-icon-gtk2',
    subdirs: 'xapp-status-icon-gtk2',
    description: 'Utility library',
    version: meson.project_version(),
    requires: ['gtk+-2.0', 'glib-2.0', 'gio-2.0', 'gobject-2.0'],
)
