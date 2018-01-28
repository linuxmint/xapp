ent_conf = configuration_data()
ent_conf.set('PACKAGE', 'xapps')
ent_conf.set('PACKAGE_BUGREPORT', 'https://github.com/linuxmint/xapps/issues/new')
ent_conf.set('PACKAGE_NAME', 'XApp')
ent_conf.set('PACKAGE_STRING', meson.project_name())
ent_conf.set('PACKAGE_TARNAME', '@0@-@1@'.format(meson.project_name(), meson.project_version()))
ent_conf.set('PACKAGE_URL', 'https://github.com/linuxmint/xapps')
ent_conf.set('PACKAGE_VERSION', meson.project_version())

configure_file(
    input: 'gtkdocentities.ent.in',
    output: 'gtkdocentities.ent',
    configuration: ent_conf
)
