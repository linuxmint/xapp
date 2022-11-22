schemas = files('org.x.apps.gschema.xml', 'org.x.apps.mate-panel-applet.gschema.xml')

install_data(schemas,
    install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas')
)

meson.add_install_script('meson_install_schemas.py')
