subdir('xml')

gnome.gtkdoc('libxapp',
    install: true,
    src_dir: [
        join_paths(meson.project_source_root(), 'libxapp'),
        join_paths(meson.project_build_root(), 'libxapp'),
    ],
    dependencies: libxapp_dep,
    gobject_typesfile: 'libxapp.types',
    main_xml: 'xapp-docs.xml',
    scan_args: ['--rebuild-types'],
    mkdb_args: ['--xml-mode', '--output-format=xml'],
    ignore_headers: ['favorite-vfs-file.h', 'favorite-vfs-file-enumerator.h', 'favorite-vfs-file-monitor.h']
)
