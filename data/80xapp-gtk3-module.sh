#!/bin/bash
# This file is sourced by xinit(1) or a display manager's Xsession, not executed.

if [ -z "$GTK3_MODULES" ] ; then
    GTK3_MODULES="xapp-gtk3-module"
else
    GTK3_MODULES="$GTK3_MODULES:xapp-gtk3-module"
fi

export GTK3_MODULES
