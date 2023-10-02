## Applet file

conf = configuration_data()
conf.set('locale', join_paths(get_option('prefix'), get_option('localedir')))
conf.set('pkgversion', meson.project_version())

# Put replacements in a separate file, and import the file into the applet.
# This is mainly to make testing/dev easier - if you put the conf replacement
# strings inside the applet itself, you have to build each time to get a valid
# file (with all replacements made).  With this, you can just copy from source,
# and your replacements are already there in this file.
constants_file = configure_file(
  input : 'applet_constants.py.in',
  output: 'applet_constants.py',
  configuration: conf,
)

libexec_files = [
    constants_file,
    'mate-xapp-status-applet.py'
]

install_data(libexec_files,
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
  input: 'org.x.MateXAppStatusApplet.mate-panel-applet.in.in',
  output: 'org.x.MateXAppStatusApplet.mate-panel-applet.in',
  configuration: conf,
)

i18n.merge_file(
  input: def_file,
  output: 'org.x.MateXAppStatusApplet.mate-panel-applet',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'mate-panel', 'applets')
)
