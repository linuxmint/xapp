#!/usr/bin/python3

import os
import sys
import subprocess

install_dir = os.path.join(os.environ['MESON_INSTALL_DESTDIR_PREFIX'], 'include', 'xapp', 'libxapp')
header_path = os.path.join(os.environ['MESON_BUILD_ROOT'], 'libxapp', sys.argv[1])

print("\nInstalling generated header '%s' to %s\n" % (sys.argv[1], install_dir))

subprocess.call(['cp', header_path, install_dir])
