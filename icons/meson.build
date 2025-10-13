install_subdir('hicolor',
    install_dir: join_paths(get_option('prefix'), get_option('datadir'), 'icons'),
)

install_data('adwaita-symbolic-icons.info', install_dir : get_option('datadir') / 'xapp')

configure_file(
  input: 'xapp-scan-code-for-adwaita-symbolic-icons.in',
  output: 'xapp-scan-code-for-adwaita-symbolic-icons',
  configuration: {
    'XAPP_DATA_DIR': join_paths(get_option('prefix'), get_option('datadir'), 'xapp'),
  },
  install: true,
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x'
)
