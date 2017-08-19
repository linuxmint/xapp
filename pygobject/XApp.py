from gi.overrides import override
from gi.importer import modules

"""
The only purpose of this file is to ensure the XApp widget GType is registered
at the time of import.  Otherwise any user of XApp.GtkWindow would have to create
a dummy widget prior to using a GtkBuilder to parse a ui file containing an
XAppGtkWindow.

The gi import machinery sweeps usr/lib/python*/dist-packages/gi/overrides for file
matching the module name at the time of execution.

This file needs to be in both python2 and python3 overrides locations.

"""

XApp = modules['XApp']._introspection_module

__all__ = []

class GtkWindow(XApp.GtkWindow):
    pass

GtkWindow = override(GtkWindow)
__all__.append('GtkWindow')
