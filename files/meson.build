message('Copying ./files structure ************')

# FIXME: 'find' in mint 18/xenial doesn't support multiple types.
#        Once we stop supporting that base, we can remove this outer
#        foreach, and make the -type argument 'l,f'

# foreach t : ['l', 'f']
#     r = run_command('find', '-mindepth', '2', '-type', t)
#     #    ./usr/bin/upload-system-info
#     #    ./usr/bin/xfce4-set-wallpaper
#     #    ...
#     if r.returncode() == 0
#         files = r.stdout().split('\n')

#         foreach f : files
#             f_stripped = f.strip()

#             if f_stripped != ''
#                 fileparts = f_stripped.split('/')

#                 parent_array = []
#                 relative_array = []
#                 x = 0

#                 foreach part : fileparts
#                     if x > 0 # skip the . part
#                         if x < (fileparts.length() - 1)
#                             parent_array += part
#                         elif x < (fileparts.length())
#                             relative_array = (parent_array + [part])
#                         endif
#                     endif

#                     x += 1
#                 endforeach

#                 relative_path = join_paths(relative_array)
#                 install_dir_path = join_paths('/', '/'.join(parent_array))

#                 message(relative_path)

#                 install_data(relative_path,
#                     install_dir: install_dir_path
#                 )
#             endif

#         endforeach
#     endif
# endforeach


install_subdir('usr',
    install_dir: '/',
)


