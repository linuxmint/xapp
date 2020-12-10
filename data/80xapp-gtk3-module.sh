#!/bin/bash
# This file is sourced by xinit(1) or a display manager's Xsession, not executed.

if [ -z "$GTK_MODULES" ] ; then
    GTK_MODULES="xapp-gtk3-module"
else
    GTK_MODULES="$GTK_MODULES:xapp-gtk3-module"
fi

export GTK_MODULES
