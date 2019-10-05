## Applet file

conf = configuration_data()
conf.set('locale', join_paths(get_option('prefix'), get_option('localedir')))

applet_file = configure_file(
  input : 'mate-xapp-status-applet.py',
  output: 'mate-xapp-status-applet.py',
  configuration: conf,
)

install_data(applet_file,
  install_dir: join_paths(get_option('libexecdir'), 'xapps')
)

## DBus service file

conf = configuration_data()
conf.set('libexec', join_paths(get_option('prefix'), get_option('libexecdir')))

service_file = configure_file(
  input : 'org.mate.panel.applet.MateXAppStatusAppletFactory.service.in',
  output: 'org.mate.panel.applet.MateXAppStatusAppletFactory.service',
  configuration: conf,
)

install_data(service_file,
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services')
)

## Applet definition file

def_file = configure_file(
  input : 'org.x.MateXAppStatusApplet.mate-panel-applet.in',
  output: 'org.x.MateXAppStatusApplet.mate-panel-applet',
  configuration: conf,
)

install_data(def_file,
  install_dir: join_paths(get_option('datadir'), 'mate-panel', 'applets')
)

