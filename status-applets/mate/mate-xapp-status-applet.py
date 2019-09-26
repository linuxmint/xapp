#!/usr/bin/python3

import locale
import gettext
import os
import sys
import setproctitle

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("XApp", "1.0")
gi.require_version('MatePanelApplet', '4.0')
from gi.repository import Gtk, GdkPixbuf, Gdk, GObject, XApp, GLib, MatePanelApplet

# Rename the process
setproctitle.setproctitle('mate-xapp-status-applet')

# i18n
gettext.install("xapp", "@locale@")
locale.bindtextdomain("xapp", "@locale@")
locale.textdomain("xapp")

class StatusWidget(Gtk.EventBox):
    def __init__(self, icon, orientation, size):
        super(Gtk.EventBox, self).__init__(visible_window=False)
        self.theme = Gtk.IconTheme.get_default()
        self.orientation = orientation
        self.size = size

        self.proxy = icon

        # this is the bus owned name
        self.name = self.proxy.get_name()

        # this is (usually) the name of the remote process
        self.proc_name = self.proxy.props.name

        self.box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL,
                           spacing=5)

        self.image = Gtk.Image(hexpand=True)
        self.image.show()
        self.label = Gtk.Label(no_show_all=True)
        self.box.pack_start(self.image, False, False, 0)
        self.box.add(self.label)
        self.add(self.box)

        flags = GObject.BindingFlags.DEFAULT | GObject.BindingFlags.SYNC_CREATE

        self.proxy.bind_property("label", self.label, "label", flags)
        self.proxy.bind_property("tooltip-text", self, "tooltip-text", flags)
        self.proxy.bind_property("visible", self, "visible", flags)

        self.proxy.connect("notify::icon-name", self._on_icon_name_changed)

        self.connect("button-press-event", self.on_button_press)
        self.connect("button-release-event", self.on_button_release)

        self.update_orientation()
        self.update_icon()

        self.show_all()

    def _on_icon_name_changed(self, proxy, gparamspec, data=None):
        self.update_icon()

    def update_icon(self):
        string = self.proxy.props.icon_name

        self.set_icon(string)

    def update_orientation(self):
        box_orientation = Gtk.Orientation.HORIZONTAL

        if self.orientation in (MatePanelApplet.AppletOrient.LEFT, MatePanelApplet.AppletOrient.RIGHT):
            box_orientation = Gtk.Orientation.VERTICAL

        self.box.set_orientation(box_orientation)
        self.label.set_visible(box_orientation == Gtk.Orientation.HORIZONTAL)

    def set_icon(self, string):
        size = self.size - 5
        scale = self.get_scale_factor()
        pixbuf_size = (size) * scale

        try:
            if os.path.exists(string):
                pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_size (string, -1, pixbuf_size)
                surf = Gdk.cairo_surface_create_from_pixbuf (pixbuf,
                                                             scale,
                                                             self.get_window())

                self.image.set_from_surface(surf)
                return
            else:
                if self.theme.has_icon(string):
                    surf = self.theme.load_surface(string,
                                                   size,
                                                   scale,
                                                   self.get_window(),
                                                   Gtk.IconLookupFlags.FORCE_SIZE)

                    self.image.set_from_surface(surf)
                    return
        except GLib.Error as e:
            print("MateXAppStatusApplet: Could not load icon '%s' for '%s': %s" % (string, self.proc_name, e.message))
        except TypeError as e:
            print("MateXAppStatusApplet: Could not load icon '%s' for '%s': %s" % (string, self.proc_name, str(e)))

        #fallback
        self.image.set_from_icon_name("image-missing", Gtk.IconSize.MENU)

    def on_button_press(self, widget, event):
        orientation = self.translate_applet_orientation_to_xapp(self.orientation)

        x, y = self.calc_menu_origin(widget, orientation)
        time = event.time

        self.proxy.call_button_press(x, y, event.button, event.time, orientation, None, None)
        return True

    def on_button_release(self, widget, event):
        orientation = self.translate_applet_orientation_to_xapp(self.orientation)

        x, y = self.calc_menu_origin(widget, orientation)
        time = event.time

        self.proxy.call_button_release(x, y, event.button, event.time, orientation, None, None)
        return True

    def calc_menu_origin(self, widget, orientation):
        alloc = widget.get_allocation()
        ignore, x, y = widget.get_window().get_origin()
        rx = 0
        ry = 0

        if orientation == Gtk.PositionType.TOP:
            rx = x + alloc.x
            ry = y + alloc.y + alloc.height
        elif orientation == Gtk.PositionType.BOTTOM:
            rx = x + alloc.x
            ry = y + alloc.y
        elif orientation == Gtk.PositionType.LEFT:
            rx = x + alloc.x + alloc.width
            ry = y + alloc.y
        elif orientation == Gtk.PositionType.RIGHT:
            rx = x + alloc.x
            ry = y + alloc.y
        else:
            rx = event.x
            ry = event.y

        return rx, ry

    def translate_applet_orientation_to_xapp(self, mate_applet_orientation):
        # wtf...mate panel's orientation is.. the direction to center of monitor?
        if mate_applet_orientation == MatePanelApplet.AppletOrient.UP:
            return Gtk.PositionType.BOTTOM
        elif mate_applet_orientation == MatePanelApplet.AppletOrient.DOWN:
            return Gtk.PositionType.TOP
        elif mate_applet_orientation == MatePanelApplet.AppletOrient.LEFT:
            return Gtk.PositionType.RIGHT
        elif mate_applet_orientation == MatePanelApplet.AppletOrient.RIGHT:
            return Gtk.PositionType.LEFT

class MateXAppStatusApplet(object):
    def __init__(self, applet, iid):
        self.applet = applet
        self.applet.set_flags(MatePanelApplet.AppletFlags.EXPAND_MINOR)
        self.applet.set_can_focus(True)

        self.indicator_box = Gtk.Box(spacing=5,
                                     border_width=2,
                                     visible=True)

        self.applet.add(self.indicator_box)
        self.applet.connect("change-size", self.on_applet_size_changed)
        self.applet.connect("change-orient", self.on_applet_orientation_changed)

        self.indicators = {}

        self.update_orientation()

        self.monitor = None
        self.setup_monitor()

    def on_window_destroy(self, widget, data=None):
        self.destroy_monitor()
        Gtk.main_quit()

    def setup_monitor (self):
        self.monitor = XApp.StatusIconMonitor()
        self.monitor.connect("icon-added", self.on_icon_added)
        self.monitor.connect("icon-removed", self.on_icon_removed)

    def destroy_monitor (self):
        for key in self.indicators.keys():
            self.indicator_box.remove(self.indicators[key])

        self.monitor = None
        self.indicators = {}

    def on_icon_added(self, monitor, proxy):
        name = proxy.get_name()

        self.indicators[name] = StatusWidget(proxy, self.applet.get_orient(), self.applet.get_size())
        self.indicator_box.add(self.indicators[name])

    def on_icon_removed(self, monitor, proxy):
        name = proxy.get_name()

        self.indicator_box.remove(self.indicators[name])
        del(self.indicators[name])

    def update_orientation(self):
        self.on_applet_orientation_changed(self, self.applet.get_orient())

    def on_applet_size_changed(self, applet, size, data=None):
        for key in self.indicators.keys():
            indicator = self.indicators[key]

            indicator.size = applet.get_size()
            indicator.update_icon()

        self.applet.queue_resize()

    def on_applet_orientation_changed(self, applet, applet_orient, data=None):
        orient = self.applet.get_orient()

        for key in self.indicators.keys():
            indicator = self.indicators[key]

            indicator.orientation = orient
            indicator.update_orientation()

        box_orientation = Gtk.Orientation.HORIZONTAL

        if orient in (MatePanelApplet.AppletOrient.LEFT, MatePanelApplet.AppletOrient.RIGHT):
            box_orientation = Gtk.Orientation.VERTICAL

        self.indicator_box.set_orientation(box_orientation)
        self.indicator_box.queue_resize()

def applet_factory(applet, iid, data):
    MateXAppStatusApplet(applet, iid)
    applet.show()
    return True

def quit_all(widget):
    Gtk.main_quit()
    sys.exit(0)

MatePanelApplet.Applet.factory_main("MateXAppStatusAppletFactory", True,
                                    MatePanelApplet.Applet.__gtype__,
                                    applet_factory, None)
