#!/usr/bin/python3
# This script lists any icon which could be missing from XApp
import os

with open("adwaita-symbolic-icons.info", "r") as flist:
	for line in flist:
		line = line.strip()
		old, cat, new = line.split(" # ")
		if (new == "null"):
			continue
		new = f"hicolor/scalable/actions/xapp-{new}-symbolic.svg"
		if not os.path.exists(new):
			print (new)

