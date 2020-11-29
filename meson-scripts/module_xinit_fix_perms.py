#!/usr/bin/python3

import os
import sys
import subprocess

if os.environ.get('DESTDIR'):
    file = "%s/%s/%s" % (os.environ.get('DESTDIR'), sys.argv[1], sys.argv[2])
else:
    file = os.path.join(sys.argv[1], sys.argv[2])

subprocess.call(['sh', '-c', 'chmod +x %s' % file])

exit(0)
