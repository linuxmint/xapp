docConf = configuration_data()
docConf.set('PACKAGE_URL', 'https://github.com/linuxmint/xapps')
docConf.set('PACKAGE_BUGREPORT', 'https://github.com/linuxmint/xapps/issues')
docConf.set('PACKAGE_VERSION', meson.project_version())
docConf.set('XAPP_ABI_VERSION', ABI_VERSION)
configure_file(input: 'gtkdocentities.ent.in', output: 'gtkdocentities.ent', configuration: docConf)

gnome.gtkdoc('libxapp',
    main_xml: 'xapp-docs.xml',
    src_dir: [
        join_paths(meson.source_root(), 'libxapp'),
        join_paths(meson.build_root(), 'libxapp')
    ],
    dependencies: libxapp_dep,
    install: true,
    gobject_typesfile: 'libxapp.types',
    scan_args: [
        '--rebuild-types'
    ],
    mkdb_args: ['--xml-mode', '--output-format=xml']
)
