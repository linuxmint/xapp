#!/usr/bin/python3

import subprocess, os

try:
    inxi = subprocess.Popen(['inxi', '-Fxxrzc0'], stdout=subprocess.PIPE)
    pastebin = subprocess.Popen(['/usr/bin/pastebin'], stdin=inxi.stdout, stdout=subprocess.PIPE)
    inxi.stdout.close()
    output = pastebin.communicate()[0]
    output = output.split()[0] # if we have more than one URL, only use the first one
    pastebin.wait()
    subprocess.call(['xdg-open', output])
except Exception as e:
    print ("An error occurred while uploading the system information:")
    print (e)
    print ("Please make sure you're connected to the Internet.")
