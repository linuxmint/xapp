from gi.overrides import override
from gi.importer import modules

XApp = modules['XApp']._introspection_module

__all__ = []

class GtkWindow(XApp.GtkWindow):

    def __init__(self, *args, **kwargs):
        XApp.GtkWindow.__init__(self, *args, **kwargs)

GtkWindow = override(GtkWindow)
__all__.append('GtkWindow')
