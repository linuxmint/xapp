glib_min_ver = '>=2.44.0'

gio_dep = dependency('gio-2.0', version: glib_min_ver, required: true)
gio_unix_dep = dependency('gio-unix-2.0', version: glib_min_ver, required: true)
glib_dep = dependency('glib-2.0', version: glib_min_ver, required: true)
gtk3_dep = dependency('gtk+-3.0', version: '>=3.22', required: true)
gmodule_dep = dependency('gmodule-2.0', version: glib_min_ver, required: true)

libdeps = []
libdeps += gio_dep
libdeps += gio_unix_dep
libdeps += glib_dep
libdeps += gtk3_dep
libdeps += gmodule_dep
libdeps += dependency('gdk-pixbuf-2.0', version: '>=2.22.0', required: true)
libdeps += dependency('cairo', required: true)
libdeps += dependency('x11', required: true)

gtk_layer_shell_dep = dependency('gtk-layer-shell-0', version: '>=0.8', required: get_option('gtk-layer-shell'))
libxapp_c_args = ['-Wno-declaration-after-statement', '-DG_LOG_DOMAIN="XApp"']
if gtk_layer_shell_dep.found()
    libdeps += gtk_layer_shell_dep
    libxapp_c_args += '-DHAVE_GTK_LAYER_SHELL=1'
endif

# XAppGtkWindow display-server backends. The X11 backend (the _NET_WM_XAPP_*
# window properties) is always built; the Wayland backend (the private
# xapp-shell protocol) is built when the wayland tooling is available. Kept out
# of the introspected sources below - they expose no public API.
xapp_backend_sources = [ 'xapp-gtk-window-x11.c' ]
xapp_wayland_generated = []

wayland_scanner_dep = dependency('wayland-scanner', required: false, native: true)
wayland_client_dep = dependency('wayland-client', required: false)
gdk_wayland_dep = dependency('gdk-wayland-3.0', required: false)

if wayland_scanner_dep.found() and wayland_client_dep.found() and gdk_wayland_dep.found()
    wayland_scanner = find_program(wayland_scanner_dep.get_variable(pkgconfig: 'wayland_scanner'),
                                   native: true)

    xapp_shell_client_header = custom_target('xapp-shell client header',
        input: 'xapp-shell.xml',
        output: 'xapp-shell-client-protocol.h',
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

    xapp_shell_code = custom_target('xapp-shell code',
        input: 'xapp-shell.xml',
        output: 'xapp-shell-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

    xapp_wayland_generated = [ xapp_shell_client_header, xapp_shell_code ]
    xapp_backend_sources += 'xapp-gtk-window-wayland.c'

    libdeps += wayland_client_dep
    libdeps += gdk_wayland_dep
    libxapp_c_args += '-DHAVE_WAYLAND=1'
endif

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
    'xapp-dark-mode-manager.h',
    'xapp-favorites.h',
    'xapp-gtk-window.h',
    'xapp-gpu-offload-helper.h',
    'xapp-icon-chooser-button.h',
    'xapp-icon-chooser-dialog.h',
    'xapp-monitor-blanker.h',
    'xapp-preferences-window.h',
    'xapp-stack-sidebar.h',
    'xapp-status-icon.h',
    'xapp-status-icon-monitor.h',
    'xapp-style-manager.h',
    'xapp-util.h',
    'xapp-visibility-group.h'
]

xapp_sources = [
    'xapp-dark-mode-manager.c',
    'xapp-favorites.c',
    'xapp-glade-catalog.c',
    'xapp-gtk-window.c',
    'xapp-gpu-offload-helper.c',
    'xapp-icon-chooser-button.c',
    'xapp-icon-chooser-dialog.c',
    'xapp-monitor-blanker.c',
    'xapp-preferences-window.c',
    'xapp-stack-sidebar.c',
    'xapp-status-icon.c',
    'xapp-status-icon-monitor.c',
    'xapp-style-manager.c',
    'xapp-util.c',
    'xapp-visibility-group.c'
]

switcheroo_sources = gnome.gdbus_codegen(
  'xapp-switcheroo-interface',
  sources: 'net.hadess.SwitcherooControl.xml',
  interface_prefix: 'net.hadess',
  namespace: 'XApp'
)

xapp_sources += switcheroo_sources

if not app_lib_only
    libdeps += dependency('libgnomekbdui', required: true)
    xapp_headers += 'xapp-kbd-layout-controller.h'
    xapp_sources += 'xapp-kbd-layout-controller.c'
endif

xapp_statusicon_interface_sources = gnome.gdbus_codegen(
  'xapp-statusicon-interface',
  sources: 'org.x.StatusIcon.xml',
  interface_prefix: 'org.x.StatusIcon',
  namespace: 'XApp',
  annotations: [[ 'org.x.StatusIcon', 'org.gtk.GDBus.C.Name', 'StatusIconInterface' ]],
  object_manager: true,
  install_header: true,
  install_dir: join_paths(get_option('prefix'), get_option('includedir'), 'xapp/libxapp')
)

dbus_headers = [ xapp_statusicon_interface_sources[1] ]
xapp_sources += xapp_statusicon_interface_sources[0]

xapp_enums = gnome.mkenums_simple('xapp-enums',
    sources : xapp_headers,
    identifier_prefix : 'XApp',
    symbol_prefix : 'xapp'
)

libxapp = library('xapp',
    sources  : xapp_headers + xapp_sources + xapp_enums + dbus_headers + favorite_vfs_sources + xapp_debug + xapp_backend_sources + xapp_wayland_generated,
    include_directories: [top_inc],
    version: meson.project_version(),
    soversion: '1',
    dependencies: libdeps,
    c_args: libxapp_c_args,
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

if get_option('introspection')
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
endif

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

if get_option('vapi')
    assert(is_variable('gir'), 'vapi requires introspection to be enabled')

    gnome.generate_vapi('xapp',
        packages: ['glib-2.0', 'gio-unix-2.0', 'gtk+-3.0'],
        sources: gir[0],
        metadata_dirs: meson.current_source_dir(),
        install: true
    )
endif

if not app_lib_only
    gtk3_module = shared_module(
        'xapp-gtk3-module', ['xapp-gtk3-module.c'],
        include_directories: [top_inc],
        dependencies: [gtk3_dep, libxapp_dep],
        install: true,
        install_dir: join_paths(gtk3_dep.get_variable(pkgconfig: 'libdir'),'gtk-3.0','modules')
    )
endif
