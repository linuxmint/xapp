#!/usr/bin/python3

import gi
from gi.repository import GObject, Gio, GLib

import util

class BusNameWatcher(GObject.Object):
    """
    We can't rely on our StatusNotifier instances to tell us when their NameOwner
    changes.
    """
    __gsignals__ = {
        "owner-lost": (GObject.SignalFlags.RUN_LAST, None, (str,str))
    }
    def __init__(self):
        """
        Connect to the bus and retrieve a list of interfaces.
        """
        super(BusNameWatcher, self).__init__()

        Gio.DBusProxy.new_for_bus(Gio.BusType.SESSION,
                                  Gio.DBusProxyFlags.NONE,
                                  None,
                                  "org.freedesktop.DBus",
                                  "/org/freedesktop/DBus",
                                  "org.freedesktop.DBus",
                                  None,
                                  self.connected)

    def connected(self, source, result, data=None):
        try:
            self.proxy = Gio.DBusProxy.new_for_bus_finish(result)
        except GLib.Error as e:
            # FIXME: what to do here?
            return

        self.proxy.connect("g-signal",
                           self.signal_received)

    def signal_received(self, proxy, sender, signal, parameters, data=None):
        if signal == "NameOwnerChanged":
            # name, old_owner, new_owner
            if parameters[2] == "":
                self.name_lost(parameters[0], parameters[1])

    @util._idle
    def name_lost(self, name, old_name_owner):
        self.emit("owner-lost", name, old_name_owner)
