pygobject = dependency('pygobject-3.0',
    version: '>=2.9.0',
    required: true,
)

override_dirs = get_option('py-overrides-dir')

if override_dirs.length() == 0 or override_dirs[0] == ''
    override_dirs = []

    pymod = import('python')
    python_targets = get_option('python_target')

    foreach python_target : python_targets
        python_install = pymod.find_installation(python_target)

        r = run_command(python_install, '-c', 'import gi;print(gi._overridesdir)', check: false)

        if r.returncode() != 0
            error('Error getting the GObject Introspection override directory: ' + r.stderr())
        endif

        override_dirs += r.stdout().strip()
    endforeach
endif

message('PyGObject overrides dirs: @0@'.format(override_dirs))

foreach override_dir : override_dirs
    install_data(['XApp.py'],
        install_dir: override_dir,
    )
endforeach
