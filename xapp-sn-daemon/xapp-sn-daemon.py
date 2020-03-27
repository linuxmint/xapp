#!/usr/bin/env python3

import sys
import gettext
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('XApp', '1.0')
from gi.repository import Gtk, Gdk, Gio, XApp, GLib
import setproctitle

from itemWrapper import SnItemWrapper

setproctitle.setproctitle("xapp-sn-daemon")

NOTIFICATION_WATCHER_NAME = "org.kde.StatusNotifierWatcher"
NOTIFICATION_WATCHER_PATH = "/StatusNotifierWatcher"


class XAppSNDaemon(Gtk.Application):
    def __init__(self):
        super(XAppSNDaemon, self).__init__(register_session=True, application_id="org.xapp.status-notifier-daemon")

        self.items = {}
        self.bus = None

    def do_activate(self):
        self.hold()

    def do_startup(self):
        Gtk.Application.do_startup(self)

        Gio.bus_own_name(Gio.BusType.SESSION,
                         NOTIFICATION_WATCHER_NAME,
                         Gio.BusNameOwnerFlags.REPLACE,
                         self.on_bus_acquired,
                         self.on_name_acquired,
                         self.on_name_lost)

    def on_name_lost(self, connection, name, data=None):
        """
        Failed to acquire our name - just exit.
        """
        print("Something is wrong, exiting.")
        Gtk.main_quit()

    def on_name_acquired(self, connection, name, data=None):

        print("Starting xapp-sn-daemon...")

    def on_bus_acquired(self, connection, name, data=None):
        self.bus = connection

        self.watcher = XApp.FdoSnWatcherSkeleton.new()

        self.watcher.props.is_status_notifier_host_registered = True
        self.watcher.props.registered_status_notifier_items = []
        self.protocol_version = 0

        self.watcher.connect("handle-register-status-notifier-item", self.handle_register_item)
        self.watcher.connect("handle-register-status-notifier-host", self.handle_register_host)

        self.watcher.export(self.bus, NOTIFICATION_WATCHER_PATH)

    def handle_register_item(self, watcher, invocation, service):
        print("register item: %s" % service)

        sender = invocation.get_sender()

        if service[0] == "/":
            bus_name = sender
            path = service
        else:
            bus_name = service
            path = "/StatusNotifierItem"

        if not Gio.dbus_is_name(bus_name):
            invocation.return_error(Gio.io_error_quark(),
                                    Gio.IOErrorEnum.INVALID_ARGUMENT,
                                    "Invalid bus name: %s" % bus_name)

            return False

        key = "%s%s" % (bus_name, path)
        # print(key, bus_name, path)
        try:
            proxy = XApp.FdoSnItemProxy.new_sync(self.bus,
                                                 Gio.DBusProxyFlags.NONE,
                                                 bus_name,
                                                 path,
                                                 None)

            wrapper = SnItemWrapper(proxy)

            try:
                existing = self.items[key]
            except KeyError:
                existing = None

            if existing:
                self.remove_item(key)

            self.items[key] = wrapper
            self.watcher.props.registered_status_notifier_items = list(self.items.keys())

        except GLib.Error as e:
            print(e.message)
            invocation.return_gerror(e)
            return False

        watcher.complete_register_status_notifier_item(invocation)
        watcher.emit_status_notifier_item_registered(service)

        return True

    def item_name_owner_changed(self, item, pspec, key):
        print("name owner changed: %s, now: '%s'" % (key, item.props.g_name_owner))
        if item.props.g_name_owner in ("", None):
            self.remove_item(key)

    def remove_item(self, key):
        try:
            item = self.items[key]

            try:
                item.disconnect_by_func(self.item_name_owner_changed)
            except:
                pass

            item.destroy()
            del self.items[key]
        except KeyError:
            print("destroying non-existent item: %s" % key)

    def handle_register_host(self, watcher, invocation, service):
        print("register host: %s" % service)

        watcher.complete_register_status_notifier_host(invocation)

        return True

    def destroy(self):
        self.watcher.unexport()
        self.quit()

if __name__ == "__main__":
    d = XAppSNDaemon().run()

    d.run(sys.argv)
