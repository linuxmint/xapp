#!/usr/bin/python3

import argparse
import os
import sys

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gio, GLib

"""
This variant implements the StatusNotifierItem spec directly on the bus, with no
toolkit helper - no Qt, no libappindicator, no XApp.StatusIcon. It mimics how
Chromium/Electron apps present a tray icon: a well-known name of the form
org.freedesktop.StatusNotifierItem-<pid>-1, the item at /StatusNotifierItem,
an ARGB pixmap rather than a themed icon name, and its own com.canonical.dbusmenu
implementation with a nested submenu.

Left click sends Activate, which toggles a window. Middle click sends
SecondaryActivate. Right click shows the menu.

Flags exercise the places hosts and clients disagree:

  --kde-only              serve only org.kde.StatusNotifierItem - the de-facto
                          name that every host in the wild actually asks for
  --fdo-only              serve only org.freedesktop.StatusNotifierItem - what the
                          published spec names. xapp-sn-watcher cannot see this
                          today; it registers but stays blank
  --registered-name-only  answer property calls only when addressed to the
                          well-known name, refusing the unique connection name.
                          Legal per the spec, and what Chromium's status icon
                          multiplexer did. Pins the watcher to addressing items
                          by the name they registered with
  --icon-name             send IconName instead of IconPixmap
"""

ITEM_PATH = "/StatusNotifierItem"
MENU_PATH = "/com/linuxmint/DbusMenu/1"

KDE_IFACE = "org.kde.StatusNotifierItem"
FDO_IFACE = "org.freedesktop.StatusNotifierItem"

WATCHER = ("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
           "org.kde.StatusNotifierWatcher")

SNI_XML = """
<node>
  <interface name='%s'>
    <property name='Category' type='s' access='read'/>
    <property name='Id' type='s' access='read'/>
    <property name='Title' type='s' access='read'/>
    <property name='Status' type='s' access='read'/>
    <property name='WindowId' type='i' access='read'/>
    <property name='IconName' type='s' access='read'/>
    <property name='IconPixmap' type='a(iiay)' access='read'/>
    <property name='AttentionIconName' type='s' access='read'/>
    <property name='OverlayIconName' type='s' access='read'/>
    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>
    <property name='ItemIsMenu' type='b' access='read'/>
    <property name='Menu' type='o' access='read'/>
    <method name='Activate'>
      <arg name='x' type='i' direction='in'/>
      <arg name='y' type='i' direction='in'/>
    </method>
    <method name='SecondaryActivate'>
      <arg name='x' type='i' direction='in'/>
      <arg name='y' type='i' direction='in'/>
    </method>
    <method name='ContextMenu'>
      <arg name='x' type='i' direction='in'/>
      <arg name='y' type='i' direction='in'/>
    </method>
    <method name='Scroll'>
      <arg name='delta' type='i' direction='in'/>
      <arg name='dir' type='s' direction='in'/>
    </method>
    <signal name='NewIcon'/>
    <signal name='NewTitle'/>
    <signal name='NewToolTip'/>
    <signal name='NewStatus'>
      <arg name='status' type='s'/>
    </signal>
  </interface>
</node>
"""

MENU_XML = """
<node>
  <interface name='com.canonical.dbusmenu'>
    <property name='Version' type='u' access='read'/>
    <property name='TextDirection' type='s' access='read'/>
    <property name='Status' type='s' access='read'/>
    <property name='IconThemePath' type='as' access='read'/>
    <method name='GetLayout'>
      <arg name='parentId' type='i' direction='in'/>
      <arg name='recursionDepth' type='i' direction='in'/>
      <arg name='propertyNames' type='as' direction='in'/>
      <arg name='revision' type='u' direction='out'/>
      <arg name='layout' type='(ia{sv}av)' direction='out'/>
    </method>
    <method name='GetGroupProperties'>
      <arg name='ids' type='ai' direction='in'/>
      <arg name='propertyNames' type='as' direction='in'/>
      <arg name='properties' type='a(ia{sv})' direction='out'/>
    </method>
    <method name='GetProperty'>
      <arg name='id' type='i' direction='in'/>
      <arg name='name' type='s' direction='in'/>
      <arg name='value' type='v' direction='out'/>
    </method>
    <method name='Event'>
      <arg name='id' type='i' direction='in'/>
      <arg name='eventId' type='s' direction='in'/>
      <arg name='data' type='v' direction='in'/>
      <arg name='timestamp' type='u' direction='in'/>
    </method>
    <method name='EventGroup'>
      <arg name='events' type='a(isvu)' direction='in'/>
      <arg name='idErrors' type='ai' direction='out'/>
    </method>
    <method name='AboutToShow'>
      <arg name='id' type='i' direction='in'/>
      <arg name='needUpdate' type='b' direction='out'/>
    </method>
    <method name='AboutToShowGroup'>
      <arg name='ids' type='ai' direction='in'/>
      <arg name='updatesNeeded' type='ai' direction='out'/>
      <arg name='idErrors' type='ai' direction='out'/>
    </method>
    <signal name='ItemsPropertiesUpdated'>
      <arg name='updatedProps' type='a(ia{sv})'/>
      <arg name='removedProps' type='a(ias)'/>
    </signal>
    <signal name='LayoutUpdated'>
      <arg name='revision' type='u'/>
      <arg name='parent' type='i'/>
    </signal>
    <signal name='ItemActivationRequested'>
      <arg name='id' type='i'/>
      <arg name='timestamp' type='u'/>
    </signal>
  </interface>
</node>
"""

MENU_ITEMS = {
    0: ([1, 2, 3, 7, 8], {}),
    1: ([], {"label": "Show test window"}),
    2: ([], {"type": "separator"}),
    3: ([4, 5, 6], {"label": "More options", "children-display": "submenu"}),
    4: ([], {"label": "Nested item"}),
    5: ([], {"label": "Disabled nested item", "enabled": False}),
    6: ([], {"label": "Another nested item"}),
    7: ([], {"type": "separator"}),
    8: ([], {"label": "Quit"}),
}


def prop_variant(key, value):
    if isinstance(value, bool):
        return GLib.Variant("b", value)
    return GLib.Variant("s", value)


def argb_pixmap(icon_name, size):
    """SNI wants ARGB32 in network byte order, which is not what GdkPixbuf hands us."""
    theme = Gtk.IconTheme.get_default()
    pixbuf = theme.load_icon(icon_name, size, Gtk.IconLookupFlags.FORCE_SIZE)
    if not pixbuf.get_has_alpha():
        pixbuf = pixbuf.add_alpha(False, 0, 0, 0)

    width = pixbuf.get_width()
    height = pixbuf.get_height()
    rowstride = pixbuf.get_rowstride()
    pixels = pixbuf.get_pixels()

    out = bytearray()
    for y in range(height):
        row = y * rowstride
        for x in range(width):
            offset = row + x * 4
            r, g, b, a = pixels[offset:offset + 4]
            out += bytes((a, r, g, b))

    return [(width, height, bytes(out))]


class Item:
    def __init__(self, args):
        self.args = args
        self.bus_name = "org.freedesktop.StatusNotifierItem-%d-1" % os.getpid()
        self.conn = None
        self.window = None
        self.attention = False

        if args.kde_only:
            self.ifaces = [KDE_IFACE]
        elif args.fdo_only:
            self.ifaces = [FDO_IFACE]
        else:
            self.ifaces = [KDE_IFACE, FDO_IFACE]

        Gio.bus_own_name(Gio.BusType.SESSION, self.bus_name,
                         Gio.BusNameOwnerFlags.NONE,
                         self.on_bus_acquired, self.on_name_acquired, self.on_name_lost)

    # ------------------------------------------------------------------ setup

    def on_bus_acquired(self, conn, name):
        self.conn = conn

        if self.args.registered_name_only:
            conn.add_filter(self.destination_filter)

        for iface in self.ifaces:
            info = Gio.DBusNodeInfo.new_for_xml(SNI_XML % iface).interfaces[0]
            conn.register_object_with_closures2(ITEM_PATH, info, self.on_item_method,
                                 self.on_item_get_property, None)
            print("serving %s at %s" % (iface, ITEM_PATH))

        menu_info = Gio.DBusNodeInfo.new_for_xml(MENU_XML).interfaces[0]
        conn.register_object_with_closures2(MENU_PATH, menu_info, self.on_menu_method,
                             self.on_menu_get_property, None)

    def on_name_acquired(self, conn, name):
        print("owning %s" % name)
        try:
            conn.call_sync(WATCHER[0], WATCHER[1], WATCHER[2],
                           "RegisterStatusNotifierItem",
                           GLib.Variant("(s)", (name,)),
                           None, Gio.DBusCallFlags.NONE, 5000, None)
            print("registered with the StatusNotifierWatcher as '%s'" % name)
        except GLib.Error as e:
            print("could not register: %s" % e.message)
            sys.exit(1)

    def on_name_lost(self, conn, name):
        print("lost %s" % name)
        sys.exit(1)

    # ----------------------------------------------------------- name filter

    def destination_filter(self, conn, message, incoming):
        """Refuse property traffic that is not addressed to our well-known name."""
        if not incoming:
            return message
        if message.get_message_type() != Gio.DBusMessageType.METHOD_CALL:
            return message
        if message.get_path() != ITEM_PATH:
            return message
        if message.get_interface() != "org.freedesktop.DBus.Properties":
            return message

        destination = message.get_destination()
        if destination == self.bus_name:
            return message

        print("refusing %s addressed to %s (expected %s)"
              % (message.get_member(), destination, self.bus_name))
        error = Gio.DBusMessage.new_method_error_literal(
            message, "org.freedesktop.DBus.Error.Failed",
            "error occurred in %s" % message.get_member())
        conn.send_message(error, Gio.DBusSendMessageFlags.NONE)
        return None

    # ------------------------------------------------------------------ item

    def on_item_get_property(self, conn, sender, path, iface, prop):
        if prop == "Category":
            return GLib.Variant("s", "ApplicationStatus")
        if prop == "Id":
            return GLib.Variant("s", "fdo-status-notifier-item")
        if prop == "Title":
            return GLib.Variant("s", "Raw StatusNotifierItem")
        if prop == "Status":
            return GLib.Variant("s", "NeedsAttention" if self.attention else "Active")
        if prop == "WindowId":
            return GLib.Variant("i", 0)
        if prop == "IconName":
            return GLib.Variant("s", "dialog-information" if self.args.icon_name else "")
        if prop == "IconPixmap":
            if self.args.icon_name:
                return GLib.Variant("a(iiay)", [])
            return GLib.Variant("a(iiay)", argb_pixmap("dialog-information", 22))
        if prop == "AttentionIconName":
            return GLib.Variant("s", "dialog-warning")
        if prop == "OverlayIconName":
            return GLib.Variant("s", "")
        if prop == "ToolTip":
            return GLib.Variant("(sa(iiay)ss)",
                                ("", [], "Raw StatusNotifierItem",
                                 "No toolkit, no appindicator - just D-Bus"))
        if prop == "ItemIsMenu":
            return GLib.Variant("b", False)
        if prop == "Menu":
            return GLib.Variant("o", MENU_PATH)
        return None

    def on_item_method(self, conn, sender, path, iface, method, params, invocation):
        args = params.unpack()
        print("%s%s" % (method, args))

        if method == "Activate":
            self.toggle_window()
        elif method == "SecondaryActivate":
            self.toggle_attention()
        elif method == "Scroll":
            print("  scrolled %d %s" % args)

        invocation.return_value(None)

    def toggle_window(self):
        if self.window is None:
            self.window = Gtk.Window(title="Raw StatusNotifierItem")
            self.window.set_default_size(320, 120)
            self.window.add(Gtk.Label(label="Activate toggles this window."))
            self.window.connect("delete-event", self.on_window_delete)
            self.window.show_all()
            print("  window shown")
        else:
            self.window.destroy()
            self.window = None
            print("  window hidden")

    def on_window_delete(self, window, event):
        self.window = None
        return False

    def toggle_attention(self):
        self.attention = not self.attention
        print("  status is now %s" % ("NeedsAttention" if self.attention else "Active"))
        for iface in self.ifaces:
            self.conn.emit_signal(None, ITEM_PATH, iface, "NewStatus",
                                  GLib.Variant("(s)", ("NeedsAttention" if self.attention
                                                       else "Active",)))
            self.conn.emit_signal(None, ITEM_PATH, iface, "NewIcon", None)

    # ------------------------------------------------------------------ menu

    def on_menu_get_property(self, conn, sender, path, iface, prop):
        if prop == "Version":
            return GLib.Variant("u", 3)
        if prop == "TextDirection":
            return GLib.Variant("s", "ltr")
        if prop == "Status":
            return GLib.Variant("s", "normal")
        if prop == "IconThemePath":
            return GLib.Variant("as", [])
        return None

    def layout(self, item_id, depth):
        children, props = MENU_ITEMS[item_id]
        variants = {}
        for key, value in props.items():
            variants[key] = prop_variant(key, value)

        kids = []
        if depth != 0:
            for child in children:
                kids.append(GLib.Variant("(ia{sv}av)", self.layout(child, depth - 1)))

        return (item_id, variants, kids)

    def on_menu_method(self, conn, sender, path, iface, method, params, invocation):
        if method == "GetLayout":
            parent, depth, _names = params.unpack()
            invocation.return_value(
                GLib.Variant("(u(ia{sv}av))", (1, self.layout(parent, depth))))
            return

        if method == "GetGroupProperties":
            ids, _names = params.unpack()
            ids = ids or list(MENU_ITEMS)
            out = []
            for item_id in ids:
                _children, props = MENU_ITEMS[item_id]
                out.append((item_id, {k: prop_variant(k, v) for k, v in props.items()}))
            invocation.return_value(GLib.Variant("(a(ia{sv}))", (out,)))
            return

        if method == "GetProperty":
            item_id, name = params.unpack()
            _children, props = MENU_ITEMS[item_id]
            invocation.return_value(
                GLib.Variant("(v)", (prop_variant(name, props.get(name, "")),)))
            return

        if method == "Event":
            item_id, event_id, _data, _timestamp = params.unpack()
            if event_id == "clicked":
                self.on_menu_clicked(item_id)
            invocation.return_value(None)
            return

        if method == "EventGroup":
            events, = params.unpack()
            for item_id, event_id, _data, _timestamp in events:
                if event_id == "clicked":
                    self.on_menu_clicked(item_id)
            invocation.return_value(GLib.Variant("(ai)", ([],)))
            return

        if method == "AboutToShow":
            invocation.return_value(GLib.Variant("(b)", (False,)))
            return

        if method == "AboutToShowGroup":
            invocation.return_value(GLib.Variant("(aiai)", ([], [])))
            return

        invocation.return_error_literal(Gio.dbus_error_quark(),
                                        Gio.DBusError.UNKNOWN_METHOD,
                                        "No such method %s" % method)

    def on_menu_clicked(self, item_id):
        _children, props = MENU_ITEMS.get(item_id, ([], {}))
        print("menu item %d clicked: %s" % (item_id, props.get("label", "?")))
        if item_id == 1:
            self.toggle_window()
        elif item_id == 8:
            sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kde-only", action="store_true")
    parser.add_argument("--fdo-only", action="store_true")
    parser.add_argument("--registered-name-only", action="store_true")
    parser.add_argument("--icon-name", action="store_true")
    args = parser.parse_args()

    if args.kde_only and args.fdo_only:
        parser.error("--kde-only and --fdo-only are mutually exclusive")

    Item(args)

    try:
        GLib.MainLoop().run()
    except KeyboardInterrupt:
        pass
    sys.exit(0)


if __name__ == '__main__':
    main()
