
if not app_lib_only

    if get_option('debian_derivative')

      install_data(['80xapp-gtk3-module.sh'],
             rename : ['80xapp-gtk3-module'],
             install_dir: join_paths(get_option('sysconfdir'), 'X11', 'Xsession.d'))
    else

      install_data(['80xapp-gtk3-module.sh'],
        install_dir: join_paths(get_option('sysconfdir'), 'X11', 'xinit', 'xinitrc.d'),
        install_mode: 'rwxr-xr-x')

    endif
endif