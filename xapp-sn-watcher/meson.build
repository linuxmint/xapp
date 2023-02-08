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


conf = configuration_data()
conf.set('launch_folder', sn_watcher_dir)

## DBus service file
service_file = configure_file(
  input : 'org.x.StatusNotifierWatcher.service.in',
  output: 'org.x.StatusNotifierWatcher.service',
  configuration: conf,
  install_dir: dbus_services_dir,
  install: true
)

## Autostart desktop file
autostart_file = configure_file(
  input : 'xapp-sn-watcher.desktop.in',
  output: 'xapp-sn-watcher.desktop',
  configuration: conf,
  install_dir: join_paths(get_option('sysconfdir'), 'xdg', 'autostart'),
  install: true
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
  install_dir: sn_watcher_dir,
  install: true
)