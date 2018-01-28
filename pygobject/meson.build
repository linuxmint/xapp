pygobject = dependency('pygobject-3.0',
    version: '>=2.9.0',
    required: true,
)

foreach exec : ['python2', 'python3']
    r = run_command(exec, '-c', 'import gi;print(gi._overridesdir)')

    if r.returncode() == 0
        override_dir = r.stdout().strip()
        install_data(['XApp.py'],
            install_dir: override_dir
        )
    endif
endforeach

