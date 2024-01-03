scripts = files(
    'pastebin',
    'upload-system-info',
    'xapp-gpu-offload')

if get_option('xfce')
	scripts += files('xfce4-set-wallpaper')
endif

install_data(scripts,
    install_dir: join_paths(get_option('prefix'), get_option('bindir')),
    install_mode: 'rwxr-xr-x'
)
