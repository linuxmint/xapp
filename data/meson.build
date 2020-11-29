xinitrc_dir = join_paths(get_option('sysconfdir'), 'X11', 'xinit', 'xinitrc.d')
filename = '80xapp-gtk3-module.sh'

install_data([filename],
  install_dir: xinitrc_dir
)

meson.add_install_script(join_paths(meson.source_root(), 'meson-scripts', 'module_xinit_fix_perms.py'),
  xinitrc_dir,
  filename
)
