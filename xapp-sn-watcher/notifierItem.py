#!/usr/bin/python3

import gi
from gi.repository import GObject, Gio, GLib, Gdk

# We shouldn't need this class but appindicator doesn't cache their properties so it's better
# to hide the ugliness of fetching properties in here. If this situation changes it will be easier
# to modify the behavior on its own.

APPINDICATOR_PATH_PREFIX = "/org/ayatana/NotificationItem/"

class SnItem(GObject.Object):
    __gsignals__ = {
        "ready": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-icon": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-status": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-menu": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-tooltip": (GObject.SignalFlags.RUN_LAST, None, ())
    }
    def __init__(self, sn_item_proxy):
        GObject.Object.__init__(self)

        self.sn_item_proxy = sn_item_proxy
        self.prop_proxy = None
        self.ready = False
        self.update_icon_id = 0

        self._status = "Active"

        Gio.DBusProxy.new_for_bus(Gio.BusType.SESSION,
                                  Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES,
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
            print(e.message)
            # FIXME: what to do here?
            return

        self.sn_item_proxy.connect("g-signal",
                                   self.signal_received)

        self.emit("ready")

    def signal_received(self, proxy, sender, signal, parameters, data=None):
        if self.prop_proxy == None:
            return

        # print("Signal from %s: %s" % (self.sn_item_proxy.get_name(), signal))
        if signal in ("NewIcon",
                      "NewAttentionIcon",
                      "NewOverlayIcon"):
            self._emit_update_icon_signal()
        elif signal == "NewStatus":
            # libappindicator sends NewStatus during its dispose phase - by the time we want to act
            # on it, we can no longer fetch the status via Get, so we'll cache the status we receive
            # in the signal, in case this happens we can send it as a default with our own update-status
            # signal.
            self._status = parameters[0]
            self.emit("update-status")
        elif signal in ("NewMenu"):
            self.emit("update-menu")
        elif signal in ("XAyatanaNewLabel",
                        "Tooltip"):
            self.emit("update-tooltip")

    def _emit_update_icon_signal(self):
        if self.update_icon_id > 0:
            GLib.source_remove(self.update_icon_id)
            self.update_icon_id = 0

        self.update_icon_id = GLib.timeout_add(25, self._emit_update_icon_cb)

    def _emit_update_icon_cb(self):
        if self.sn_item_proxy != None:
            self.emit("update-icon")

        self.update_icon_id = 0
        return GLib.SOURCE_REMOVE

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
                print("Couldn't get %s property: %s... or this is libappindicator's closing Status update" % (name, e.message))

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
    def status               (self): return self._get_string_prop("Status", self._status)
    def menu                (self): return self._get_string_prop("Menu")
    def item_is_menu        (self): return self._get_bool_prop  ("ItemIsMenu")
    def icon_theme_path     (self): return self._get_string_prop("IconThemePath", None)
    def icon_name           (self): return self._get_string_prop("IconName", None)
    def icon_pixmap         (self): return self._get_pixmap_array_prop("IconPixmap", None)
    def att_icon_name       (self): return self._get_string_prop("AttentionIconName", None)
    def att_icon_pixmap     (self): return self._get_pixmap_array_prop("AttentionIconPixmap", None)
    def overlay_icon_name   (self): return self._get_string_prop("OverlayIconName", None)
    def overlay_icon_pixmap (self): return self._get_pixmap_array_prop("OverlayIconPixmap", None)
    def tooltip             (self):
        # For now only appindicator seems to provide anything remotely like a tooltip
        if self.sn_item_proxy.get_object_path().startswith(APPINDICATOR_PATH_PREFIX):
            return self._get_string_prop("XAyatanaLabel")
        else:
            # For everything else, no tooltip
            return ""

    def activate(self, button, x, y):
        if button == Gdk.BUTTON_PRIMARY:
            try:
                # This sucks, nothing is consistent.  Most programs don't have a primary
                # activate (all appindicator ones).  One that I checked that does, claims
                # (according to proxyinfo.get_method_info()) it only accepts SecondaryActivate,
                # but only listens for "Activate", so we attempt a sync primary call, and async
                # secondary if needed.  Otherwise we're waiting for the first to finish in a
                # callback before we can try the secondary.  Maybe we just call secondary alwayS??
                self.sn_item_proxy.call_activate_sync(x, y, None)
            except GLib.Error:
                self.sn_item_proxy.call_secondary_activate(x, y, None, None)
        elif button == Gdk.BUTTON_MIDDLE:
            self.sn_item_proxy.call_secondary_activate(x, y, None, None)

    def show_context_menu(self, button, x, y):
        if button == Gdk.BUTTON_SECONDARY:
            self.sn_item_proxy.call_context_menu(x, y, None, None)

    def scroll(self, delta, o_str):
        self.sn_item_proxy.call_scroll(delta, o_str, None, None)

    def destroy(self):
        try:
            self.sn_item_proxy.disconnect_by_func(self.signal_received)
            self.prop_proxy = None
        except Exception as e:
            print(str(e))
