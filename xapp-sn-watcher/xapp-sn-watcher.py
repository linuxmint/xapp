#!/usr/bin/env python3

import sys
import gettext
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('XApp', '1.0')
from gi.repository import Gtk, Gdk, Gio, XApp, GLib
import setproctitle

from itemWrapper import SnItemWrapper
from nameWatcher import BusNameWatcher

setproctitle.setproctitle("xapp-sn-watcher")

NOTIFICATION_WATCHER_NAME = "org.kde.StatusNotifierWatcher"
NOTIFICATION_WATCHER_PATH = "/StatusNotifierWatcher"
STATUS_ICON_MONITOR_PREFIX = "org.x.StatusIconMonitor"

class XAppSNDaemon(Gtk.Application):
    def __init__(self):
        super(XAppSNDaemon, self).__init__(register_session=True, application_id="org.x.StatusNotifierWatcher")

        self.items = {}
        self.watcher = None
        self.bus = None
        self.bus_watcher = None
        self.shutdown_timer_id = 0

    def do_activate(self):
        self.hold()

    def do_startup(self):
        Gtk.Application.do_startup(self)

        self.bus_watcher = BusNameWatcher()
        self.bus_watcher.connect("owner-lost", self.name_owner_lost)
        self.bus_watcher.connect("owner-appeared", self.name_owner_appeared)

        # Don't bother to continue if there are no monitors
        if XApp.StatusIcon.any_monitors():
            self.continue_startup()
        else:
            print("No active monitors, exiting in 30s")
            self.start_shutdown_timer()

    def continue_startup(self):
        self.hold()

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
        self.shutdown()

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
        sender = invocation.get_sender()
        # print("register item: %s,  %s" % (service, sender))

        key, bus_name, path = self.create_key(sender, service)

        try:
            try:
                existing = self.items[key]
            except KeyError:
                existing = None

            if not existing:
                proxy = XApp.FdoSnItemProxy.new_sync(self.bus,
                                                     Gio.DBusProxyFlags.NONE,
                                                     bus_name,
                                                     path,
                                                     None)

                wrapper = SnItemWrapper(proxy)

                self.items[key] = wrapper
                self.update_published_items()

        except GLib.Error as e:
            print(e.message)
            invocation.return_gerror(e)
            return False

        watcher.complete_register_status_notifier_item(invocation)
        watcher.emit_status_notifier_item_registered(service)

        return True

    def create_key(self, sender, service):
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

            return None, None

        key = "%s%s" % (bus_name, path)

        # print("Key: '%s'  -  from busname '%s', path '%s'" % (key, bus_name, path))

        return key, bus_name, path

    def update_published_items(self):
        self.watcher.props.registered_status_notifier_items = list(self.items.keys())

    def name_owner_lost(self, watcher, name, old_owner):
        for key in self.items.keys():
            if key.startswith(name):
                # print("'%s' left the bus, owned by %s" % (name, old_owner))
                self.remove_item(key)
                return

        if name.startswith(STATUS_ICON_MONITOR_PREFIX):
            # We lost a consumer, we'll check if there are any more and exit if not
            print("Lost a monitor, checking for any more")

            if XApp.StatusIcon.any_monitors():
                return
            else:
                print("Lost our last monitor, starting countdown")
                self.start_shutdown_timer()

    def name_owner_appeared(self, watcher, name, new_owner):
        if name.startswith(STATUS_ICON_MONITOR_PREFIX):
            print("A monitor appeared on the bus")
            self.cancel_shutdown_timer()

            # finish setting up if we haven't yet
            if self.watcher == None:
                self.continue_startup()

    def cancel_shutdown_timer(self):
        if self.shutdown_timer_id > 0:
            GLib.source_remove(self.shutdown_timer_id)
            self.shutdown_timer_id = 0

    def start_shutdown_timer(self):
        self.cancel_shutdown_timer()

        self.shutdown_timer_id = GLib.timeout_add_seconds(30, self.delayed_exit_timeout)

    def delayed_exit_timeout(self):
        if not XApp.StatusIcon.any_monitors():
            print("No monitors after 30s, exiting")
            self.shutdown()

        return GLib.SOURCE_REMOVE

    def remove_item(self, key):
        try:
            item = self.items[key]

            try:
                item.disconnect_by_func(self.item_name_owner_changed)
            except:
                pass

            item.destroy()
            del self.items[key]

            self.update_published_items()

        except KeyError:
            print("destroying non-existent item: %s" % key)

    def handle_register_host(self, watcher, invocation, service):
        watcher.complete_register_status_notifier_host(invocation)

        return True

    def shutdown(self):
        keys = list(self.items.keys())

        for key in keys:
            self.remove_item(key)

        if self.watcher:
            self.watcher.unexport()

        self.quit()

if __name__ == "__main__":
    d = XAppSNDaemon()

    try:
        d.run(sys.argv)
    except KeyboardInterrupt:
        Gtk.main_quit()
