
libexec_files = [
    'itemWrapper.py',
    'nameWatcher.py',
    'notifierItem.py',
    'util.py',
    'xapp-sn-watcher.py'
]

libexec_path = join_paths(get_option('prefix'), get_option('libexecdir'), 'xapps', 'sn-watcher')

install_data(libexec_files,
  install_dir: libexec_path
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
