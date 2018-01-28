schema_files = [
    'org.x.apps.gschema.xml.in.in'
]

i18n.merge_file(
  input: 'org.x.apps.gschema.xml.in',
  output: 'org.x.apps.gschema.xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas')
)

meson.add_install_script('meson_install_schemas.py')
