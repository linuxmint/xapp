#!/usr/bin/python3

import gi
from gi.repository import GObject, Gio, GLib

# We shouldn't need this class but appindicator doesn't cache their properties so it's better
# to hide the ugliness of fetching properties in here. If this situation changes it will be easier
# to modify the behavior on its own.

class SnItem(GObject.Object):
    __gsignals__ = {
        "ready": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-icon": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-status": (GObject.SignalFlags.RUN_LAST, None, (str,)),
        "update-menu": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-title": (GObject.SignalFlags.RUN_LAST, None, (str,))
    }
    def __init__(self, sn_item_proxy):
        GObject.Object.__init__(self)

        self.sn_item_proxy = sn_item_proxy
        self.prop_proxy = None
        self.ready = False

        Gio.DBusProxy.new_for_bus(Gio.BusType.SESSION,
                                  Gio.DBusProxyFlags.NONE,
                                  None,
                                  self.sn_item_proxy.get_name(),
                                  self.sn_item_proxy.get_object_path(),
                                  "org.freedesktop.DBus.Properties",
                                  None,
                                  self.prop_proxy_acquired)

    def prop_proxy_acquired(self, source, result, data=None):
        try:
            self.prop_proxy = Gio.DBusProxy.new_for_bus_finish(result)
        except GLib.Error as e:
            # FIXME: what to do here?
            return

        self.sn_item_proxy.connect("g-signal",
                                   self.signal_received)

        self.emit("ready")

    def signal_received(self, proxy, sender, signal, parameters, data=None):
        if signal in ("NewIcon",
                      "NewAttentionIcon",
                      "NewOverlayIcon"):
            self.emit("update-icon")
        elif signal == "NewStatus":
            self.emit("update-status", parameters.get_string())
        elif signal in ("NewMenu"):
            self.emit("update-menu")
        elif signal == "NewTitle":
            self.emit("update-title")

    def _get_property(self, name):
        res = self.prop_proxy.call_sync("Get",
                                        GLib.Variant("(ss)",
                                                     (self.sn_item_proxy.get_interface_name(),
                                                      name)),
                                        Gio.DBusCallFlags.NONE,
                                        5 * 1000,
                                        None)

        return res

    def _get_string_prop(self, name, default=""):
        try:
            res = self._get_property(name)
            if res[0] == "":
                return default
            return res[0]
        except GLib.Error as e:
            if e.code != Gio.DBusError.INVALID_ARGS:
                print("Couldn't get %s property: %s" % (name, e.message))

            return default

    def _get_bool_prop(self, name, default=False):
        try:
            res = self._get_property(name)

            return res[0]
        except GLib.Error as e:
            if e.code != Gio.DBusError.INVALID_ARGS:
                print("Couldn't get %s property: %s" % (name, e.message))
            return default

    def _get_pixmap_array_prop(self, name, default=None):
        try:
            res = self._get_property(name)
            if res[0] == "":
                return default

            return res[0]
        except GLib.Error as e:
            if e.code != Gio.DBusError.INVALID_ARGS:
                print("Couldn't get %s property: %s" % (name, e.message))
            return default

    def category            (self): return self._get_string_prop("Category", "ApplicationStatus")
    def id                  (self): return self._get_string_prop("Id")
    def title               (self): return self._get_string_prop("Title")
    def status              (self): return self._get_string_prop("Status", "Active")
    def menu                (self): return self._get_string_prop("Menu")
    def item_is_menu        (self): return self._get_bool_prop  ("ItemIsMenu")
    def icon_theme_path     (self): return self._get_string_prop("IconThemePath", None)
    def icon_name           (self): return self._get_string_prop("IconName", None)
    def icon_pixmap         (self): return self._get_pixmap_array_prop("IconPixmap", None)
    def att_icon_name       (self): return self._get_string_prop("AttentionIconName", None)
    def att_icon_pixmap     (self): return self._get_pixmap_array_prop("AttentionIconPixmap", None)
    def overlay_icon_name   (self): return self._get_string_prop("OverlayIconName", None)
    def overlay_icon_pixmap (self): return self._get_pixmap_array_prop("OverlayIconPixmap", None)

