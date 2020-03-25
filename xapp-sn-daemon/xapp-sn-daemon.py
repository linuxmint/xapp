#!/usr/bin/env python3

import sys
import gettext
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('XApp', '1.0')
from gi.repository import Gtk, Gdk, Gio, XApp
import setproctitle

setproctitle.setproctitle("xapp-sn-daemon")

NOTIFICATION_WATCHER_NAME = "org.kde.StatusNotifierWatcher"
NOTIFICATION_WATCHER_PATH = "/StatusNotifierWatcher"
#define NOTIFICATION_WATCHER_DBUS_IFACE   "org.kde.StatusNotifierWatcher"

#define NOTIFICATION_ITEM_DBUS_IFACE      "org.kde.StatusNotifierItem"
#define NOTIFICATION_ITEM_DEFAULT_OBJ     "/StatusNotifierItem"

#define NOTIFICATION_APPROVER_DBUS_IFACE  "org.ayatana.StatusNotifierApprover"

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
        """
        Acquired our name - pass... The real work will begin
        on our bus_acquired callback.
        """
        print("Starting xapp-sn-daemon...")

    def on_bus_acquired(self, connection, name, data=None):
        """
        Export our interface to the session bus.  Creates the
        ScreensaverManager.  We are now ready to respond to requests
        by cinnamon-session and cinnamon-screensaver-command.
        """
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
            item = XApp.FdoSnItemProxy.new(self.bus,
                                           Gio.DBusProxyFlags.NONE,
                                           bus_name,
                                           path,
                                           None)

            self.items[key] = item
            self.watcher.props.registered_status_notifier_items = list(self.items.keys())

        except GLib.Error as e:
            invocation.return_gerror(e)
            return False

        watcher.complete_register_status_notifier_item(invocation)
        watcher.emit_status_notifier_item_registered(service)

        return True

    def handle_register_host(self, watcher, invocation, service):
        print("register host: %s" % service)

        watcher.complete_register_status_notifier_host(invocation)

        return True

    def destroy(self):
        self.watcher.unexport()
        self.quit()

if __name__ == "__main__":
    d = XAppSNDaemon().run()

    try:
        d.run(sys.argv)
    except KeyboardInterrupt:
        print("interrupt")
        d.destroy()

