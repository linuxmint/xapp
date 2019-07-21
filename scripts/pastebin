#!/usr/bin/python3

import sys
import os
import stat
import subprocess

content = ""

mode = os.fstat(0).st_mode
if stat.S_ISFIFO(mode):
    content = sys.stdin.read()
elif stat.S_ISREG(mode):
    content = sys.stdin.read()
else:
    args = sys.argv[1:]
    if len(args) == 1 and os.path.exists(args[0]):
        with open(args[0], 'r') as infile:
            content = infile.read()
    else:
        str_args = ' '.join(args)
        content = str_args

if content != "":
    if os.path.exists('/usr/bin/fpaste'):
        p = subprocess.Popen(['/usr/bin/fpaste'], stdin=subprocess.PIPE)
        p.communicate(content.encode("UTF-8"))
    else:
        p = subprocess.Popen(['nc', 'termbin.com', '9999'], stdin=subprocess.PIPE)
        p.communicate(content.encode("UTF-8"))
