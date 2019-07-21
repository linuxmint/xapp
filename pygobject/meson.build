pygobject = dependency('pygobject-3.0',
    version: '>=2.9.0',
    required: true,
)

override_dir = get_option('py-overrides-dir')
exec = find_program(['python3', 'python'])

if override_dir == ''
    r = run_command(exec, '-c', 'import gi;print(gi._overridesdir)')

    if r.returncode() != 0
         error('Error getting the GObject Introspection override directory: ' + r.stderr())
    endif

    override_dir = r.stdout().strip()
endif

install_data(['XApp.py'],
    install_dir: override_dir,
)
