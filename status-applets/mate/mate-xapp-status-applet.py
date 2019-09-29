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
from gi.repository import Gtk, GdkPixbuf, Gdk, GObject, Gio, XApp, GLib, MatePanelApplet

# Rename the process
setproctitle.setproctitle('mate-xapp-status-applet')

# i18n
gettext.install("xapp", "@locale@")
locale.bindtextdomain("xapp", "@locale@")
locale.textdomain("xapp")

INDICATOR_BOX_BORDER = 1
INDICATOR_BOX_BORDER_COMP = INDICATOR_BOX_BORDER + 1
ICON_SIZE_REDUCTION = (INDICATOR_BOX_BORDER * 2)
ICON_SPACING = 5
VISIBLE_LABEL_MARGIN = 5 # When an icon has a label, add a margin between icon and label

statusicon_css_string = """
.statuswidget {
    border: none;
    padding-top: 0;
    padding-left: 4px;
    padding-bottom: 0;
    padding-right: 4px;
}
"""

def translate_applet_orientation_to_xapp(mate_applet_orientation):
    # wtf...mate panel's orientation is.. the direction to center of monitor?
    if mate_applet_orientation == MatePanelApplet.AppletOrient.UP:
        return Gtk.PositionType.BOTTOM
    elif mate_applet_orientation == MatePanelApplet.AppletOrient.DOWN:
        return Gtk.PositionType.TOP
    elif mate_applet_orientation == MatePanelApplet.AppletOrient.LEFT:
        return Gtk.PositionType.RIGHT
    elif mate_applet_orientation == MatePanelApplet.AppletOrient.RIGHT:
        return Gtk.PositionType.LEFT

class StatusWidget(Gtk.Button):
    def __init__(self, icon, orientation, size):
        super(Gtk.Button, self).__init__()
        self.theme = Gtk.IconTheme.get_default()
        self.orientation = orientation
        self.size = size

        self.get_style_context().add_class("statuswidget")

        self.proxy = icon

        # this is the bus owned name
        self.name = self.proxy.get_name()

        # this is (usually) the name of the remote process
        self.proc_name = self.proxy.props.name

        self.box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)

        self.image = Gtk.Image(hexpand=True)
        self.image.show()
        self.label = Gtk.Label(no_show_all=True)
        self.box.pack_start(self.image, False, False, 0)
        self.box.pack_start(self.label, False, False, 0)
        self.add(self.box)

        flags = GObject.BindingFlags.DEFAULT | GObject.BindingFlags.SYNC_CREATE

        self.proxy.bind_property("label", self.label, "label", flags)
        self.proxy.bind_property("tooltip-text", self, "tooltip-text", flags)
        self.proxy.bind_property("visible", self, "visible", flags)

        self.proxy.connect("notify::icon-name", self._on_icon_name_changed)

        self.in_widget = False
        self.plain_surface = None
        self.saturated_surface = None

        self.connect("button-press-event", self.on_button_press)
        self.connect("button-release-event", self.on_button_release)
        self.connect("enter-notify-event", self.on_enter_notify)
        self.connect("leave-notify-event", self.on_leave_notify)

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

        if len(self.label.props.label) > 0 and box_orientation == Gtk.Orientation.HORIZONTAL:
            self.label.set_visible(True)
            self.label.set_margin_start(VISIBLE_LABEL_MARGIN)
        else:
            self.label.set_visible(False)
            self.label.set_margin_start(0)

    def set_icon(self, string):
        size = self.size - ICON_SIZE_REDUCTION

        # round down to even
        if size % 2 != 0:
            size -= 1

        self.image.set_pixel_size(size)

        try:
            if os.path.exists(string):
                icon_file = Gio.File.new_for_path(string)
                icon = Gio.FileIcon.new(icon_file)
                self.image.set_from_gicon(icon, Gtk.IconSize.MENU)
                return
            else:
                if self.theme.has_icon(string):
                    icon = Gio.ThemedIcon.new(string)
                    self.image.set_from_gicon(icon, Gtk.IconSize.MENU)
                    return
        except GLib.Error as e:
            print("MateXAppStatusApplet: Could not load icon '%s' for '%s': %s" % (string, self.proc_name, e.message))
        except TypeError as e:
            print("MateXAppStatusApplet: Could not load icon '%s' for '%s': %s" % (string, self.proc_name, str(e)))

        #fallback
        self.image.set_from_icon_name("image-missing", Gtk.IconSize.MENU)

    # TODO?
    def on_enter_notify(self, widget, event):
        self.in_widget = True

        return Gdk.EVENT_PROPAGATE

    def on_leave_notify(self, widget, event):
        self.in_widget = False

        return Gdk.EVENT_PROPAGATE
    # /TODO

    def on_button_press(self, widget, event):
        orientation = translate_applet_orientation_to_xapp(self.orientation)

        x, y = self.calc_menu_origin(widget, orientation)
        self.proxy.call_button_press(x, y, event.button, event.time, orientation, None, None)

        self.set_state_flags(Gtk.StateFlags.SELECTED, False)

        if event.button == 3:
            return Gdk.EVENT_STOP

        return Gdk.EVENT_PROPAGATE

    def on_button_release(self, widget, event):
        orientation = translate_applet_orientation_to_xapp(self.orientation)

        x, y = self.calc_menu_origin(widget, orientation)
        self.proxy.call_button_release(x, y, event.button, event.time, orientation, None, None)
        self.set_state_flags(Gtk.StateFlags.SELECTED, False)

        if event.button == 3:
            return Gdk.EVENT_STOP

        return Gdk.EVENT_PROPAGATE

    def calc_menu_origin(self, widget, orientation):
        alloc = widget.get_allocation()
        ignore, x, y = widget.get_window().get_origin()
        rx = 0
        ry = 0

        if orientation == Gtk.PositionType.TOP:
            rx = x + alloc.x
            ry = y + alloc.y + alloc.height + INDICATOR_BOX_BORDER_COMP
        elif orientation == Gtk.PositionType.BOTTOM:
            rx = x + alloc.x
            ry = y + alloc.y - INDICATOR_BOX_BORDER_COMP
        elif orientation == Gtk.PositionType.LEFT:
            rx = x + alloc.x + alloc.width + INDICATOR_BOX_BORDER_COMP
            ry = y + alloc.y
        elif orientation == Gtk.PositionType.RIGHT:
            rx = x + alloc.x - INDICATOR_BOX_BORDER_COMP
            ry = y + alloc.y
        else:
            rx = x
            ry = y

        return rx, ry

class MateXAppStatusApplet(object):
    def __init__(self, applet, iid):
        self.applet = applet
        self.applet.set_flags(MatePanelApplet.AppletFlags.EXPAND_MINOR)
        self.applet.set_can_focus(False)
        self.applet.set_background_widget(self.applet)

        button_css = Gtk.CssProvider()

        if button_css.load_from_data(statusicon_css_string.encode()):
            Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), button_css, 600)

        self.applet.connect("realize", self.on_applet_realized)
        self.applet.connect("destroy", self.on_applet_destroy)

        self.indicators = {}
        self.monitor = None

    def on_applet_realized(self, widget, data=None):
        self.indicator_box = Gtk.Box(spacing=ICON_SPACING,
                                     visible=True,
                                     border_width=INDICATOR_BOX_BORDER)

        self.applet.add(self.indicator_box)
        self.applet.connect("change-size", self.on_applet_size_changed)
        self.applet.connect("change-orient", self.on_applet_orientation_changed)
        self.update_orientation()

        if not self.monitor:
            self.setup_monitor()

    def on_applet_destroy(self, widget, data=None):
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

        self.sort_icons()

    def on_icon_removed(self, monitor, proxy):
        name = proxy.get_name()

        self.indicator_box.remove(self.indicators[name])
        del(self.indicators[name])

        self.sort_icons()

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

    def sort_icons(self):
        icon_list = list(self.indicators.values())

        # for i in icon_list:
        #     print("before: ", i.proxy.props.icon_name, i.proxy.props.name.lower())

        icon_list.sort(key=lambda icon: icon.proxy.props.name.lower())
        icon_list.sort(key=lambda icon: icon.proxy.props.icon_name.lower().endswith("symbolic"))

        # for i in icon_list:
        #     print("after: ", i.proxy.props.icon_name, i.proxy.props.name.lower())

        icon_list.reverse()

        for icon in icon_list:
            self.indicator_box.reorder_child(icon, 0)

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
