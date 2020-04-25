sn_watcher_generated = gnome.gdbus_codegen(
  'sn-watcher-interface',
  'sn-watcher.xml',
  interface_prefix: 'org.x.'
)

sn_item_generated = gnome.gdbus_codegen(
  'sn-item-interface',
  'sn-item.xml',
  interface_prefix: 'org.x.'
)

## DBus service file

conf = configuration_data()
conf.set('launch_folder', libexec_path)

service_file = configure_file(
  input : 'org.x.StatusNotifierWatcher.service.in',
  output: 'org.x.StatusNotifierWatcher.service',
  configuration: conf,
)

install_data(service_file,
  install_dir: dbus_services_dir
)

dbusmenu = dependency('dbusmenu-gtk3-0.4', required: true)
cairo = dependency('cairo-gobject', required: true)

watcher_sources = [
  sn_watcher_generated,
  sn_item_generated,
  'xapp-sn-watcher.c',
  'sn-item.c'
]

watcher = executable('xapp-sn-watcher',
  watcher_sources,
  include_directories: [ top_inc ],
  dependencies: [libxapp_dep, dbusmenu, cairo],
  install_dir: libexec_path,
  install: true
)