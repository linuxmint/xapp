#!/usr/bin/python3

import locale
import gettext
import json
import os
import sys
import setproctitle

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("XApp", "1.0")
gi.require_version('MatePanelApplet', '4.0')
from gi.repository import Gtk, GdkPixbuf, Gdk, GObject, Gio, XApp, GLib, MatePanelApplet

import applet_constants

# Rename the process
setproctitle.setproctitle('mate-xapp-status-applet')

# i18n
gettext.install("xapp", applet_constants.LOCALEDIR)
locale.bindtextdomain("xapp", applet_constants.LOCALEDIR)
locale.textdomain("xapp")

ICON_SIZE_REDUCTION = 2
VISIBLE_LABEL_MARGIN = 5 # When an icon has a label, add a margin between icon and label
SYMBOLIC_ICON_SIZE = 22

statusicon_css_string = """
.statuswidget-horizontal {
    border: none;
    padding-top: 0;
    padding-left: 2px;
    padding-bottom: 0;
    padding-right: 2px;
}
.statuswidget-vertical {
    border: none;
    padding-top: 2px;
    padding-left: 0;
    padding-bottom: 2px;
    padding-right: 0;
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

class StatusWidget(Gtk.ToggleButton):
    __gsignals__ = {
        "re-sort": (GObject.SignalFlags.RUN_LAST, None, ())
    }

    def __init__(self, icon, orientation, size):
        super(Gtk.ToggleButton, self).__init__()
        self.theme = Gtk.IconTheme.get_default()
        self.orientation = orientation
        self.size = size

        self.proxy = icon
        self.proxy.props.icon_size = size

        # this is the bus owned name
        self.name = self.proxy.get_name()

        self.add_events(Gdk.EventMask.SCROLL_MASK)

        # this is (usually) the name of the remote process
        self.proc_name = self.proxy.props.name

        self.box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)

        self.image = Gtk.Image(hexpand=True)
        self.label = Gtk.Label(no_show_all=True)
        self.box.pack_start(self.image, True, False, 0)
        self.box.pack_start(self.label, False, False, 0)
        self.add(self.box)

        self.set_can_default(False)
        self.set_can_focus(False)
        self.set_relief(Gtk.ReliefStyle.NONE)
        self.set_focus_on_click(False)

        self.show_all()

        flags = GObject.BindingFlags.DEFAULT | GObject.BindingFlags.SYNC_CREATE

        self.proxy.bind_property("label", self.label, "label", flags)
        self.proxy.bind_property("tooltip-text", self, "tooltip-markup", flags)
        self.proxy.bind_property("visible", self, "visible", flags)

        self.proxy.connect("notify::primary-menu-is-open", self.menu_state_changed)
        self.proxy.connect("notify::secondary-menu-is-open", self.menu_state_changed)

        self.highlight_both_menus = False

        if self.proxy.props.metadata not in ("", None):
            try:
                meta = json.loads(self.proxy.props.metadata)
                if meta["highlight-both-menus"]:
                    self.highlight_both_menus = True
            except json.JSONDecodeError as e:
                print("Could not read metadata: %s" % e)

        self.proxy.connect("notify::icon-name", self._on_icon_name_changed)
        self.proxy.connect("notify::name", self._on_name_changed)

        self.in_widget = False
        self.plain_surface = None
        self.saturated_surface = None

        self.menu_opened = False

        self.connect("button-press-event", self.on_button_press)
        self.connect("button-release-event", self.on_button_release)
        self.connect("scroll-event", self.on_scroll)
        self.connect("enter-notify-event", self.on_enter_notify)
        self.connect("leave-notify-event", self.on_leave_notify)

        self.update_orientation()
        self.update_icon()

    def _on_icon_name_changed(self, proxy, gparamspec, data=None):
        self.update_icon()

    def _on_name_changed(self, proxy, gparamspec, data=None):
        self.emit("re-sort")

    def update_icon(self):
        string = self.proxy.props.icon_name
        self.proxy.props.icon_size = self.size

        self.set_icon(string)

    def update_style(self, orientation):
        ctx = self.get_style_context()

        if orientation == Gtk.Orientation.HORIZONTAL:
            ctx.remove_class("statuswidget-vertical")
            ctx.add_class("statuswidget-horizontal")
        else:
            ctx.remove_class("statuswidget-horizontal")
            ctx.add_class("statuswidget-vertical")

    def update_orientation(self):
        if self.orientation in (MatePanelApplet.AppletOrient.UP, MatePanelApplet.AppletOrient.DOWN):
            box_orientation = Gtk.Orientation.HORIZONTAL
        else:
            box_orientation = Gtk.Orientation.VERTICAL

        self.update_style(box_orientation)
        self.box.set_orientation(box_orientation)

        if len(self.label.props.label) > 0 and box_orientation == Gtk.Orientation.HORIZONTAL:
            self.label.set_visible(True)
            self.label.set_margin_start(VISIBLE_LABEL_MARGIN)
        else:
            self.label.set_visible(False)
            self.label.set_margin_start(0)

    def set_icon(self, string):
        fallback = True

        if string:
            if "symbolic" in string:
                size = SYMBOLIC_ICON_SIZE
            else:
                size = self.size - ICON_SIZE_REDUCTION

            self.image.set_pixel_size(size)

            try:
                if os.path.exists(string):
                    icon_file = Gio.File.new_for_path(string)
                    icon = Gio.FileIcon.new(icon_file)
                    self.image.set_from_gicon(icon, Gtk.IconSize.MENU)
                else:
                    if self.theme.has_icon(string):
                        icon = Gio.ThemedIcon.new(string)
                        self.image.set_from_gicon(icon, Gtk.IconSize.MENU)

                fallback = False
            except GLib.Error as e:
                print("MateXAppStatusApplet: Could not load icon '%s' for '%s': %s" % (string, self.proc_name, e.message))
            except TypeError as e:
                print("MateXAppStatusApplet: Could not load icon '%s' for '%s': %s" % (string, self.proc_name, str(e)))

        #fallback
        if fallback:
            self.image.set_pixel_size(self.size - ICON_SIZE_REDUCTION)
            self.image.set_from_icon_name("image-missing", Gtk.IconSize.MENU)

    def menu_state_changed(self, proxy, pspec, data=None):
        if pspec.name == "primary-menu-is-open":
            prop = proxy.props.primary_menu_is_open
        else:
            prop = proxy.props.secondary_menu_is_open

        if not self.menu_opened or prop == False:
            self.set_active(False)
            return

        self.set_active(prop)
        self.menu_opened = False

    # TODO?
    def on_enter_notify(self, widget, event):
        self.in_widget = True

        return Gdk.EVENT_PROPAGATE

    def on_leave_notify(self, widget, event):
        self.in_widget = False

        return Gdk.EVENT_PROPAGATE
    # /TODO

    def on_button_press(self, widget, event):
        self.menu_opened = False

        # If the user does ctrl->right-click, open the applet's about menu
        # instead of sending to the app.
        if event.state & Gdk.ModifierType.CONTROL_MASK and event.button == Gdk.BUTTON_SECONDARY:
           return Gdk.EVENT_PROPAGATE

        orientation = translate_applet_orientation_to_xapp(self.orientation)

        x, y = self.calc_menu_origin(widget, orientation)
        self.proxy.call_button_press(x, y, event.button, event.time, orientation, None, None)

        if event.button in (Gdk.BUTTON_MIDDLE, Gdk.BUTTON_SECONDARY):
            # Block the 'remove from panel' menu, and the middle-click drag.
            # They can still accomplish these things along the edges of the applet
            return Gdk.EVENT_STOP

        return Gdk.EVENT_STOP

    def on_button_release(self, widget, event):
        orientation = translate_applet_orientation_to_xapp(self.orientation)

        if event.button == Gdk.BUTTON_PRIMARY:
            self.menu_opened = True
        elif event.button == Gdk.BUTTON_SECONDARY and self.highlight_both_menus:
            self.menu_opened = True

        x, y = self.calc_menu_origin(widget, orientation)
        self.proxy.call_button_release(x, y, event.button, event.time, orientation, None, None)

        return Gdk.EVENT_PROPAGATE

    def on_scroll(self, widget, event):
        has, direction = event.get_scroll_direction()

        x_dir = XApp.ScrollDirection.UP
        delta = 0

        if direction != Gdk.ScrollDirection.SMOOTH:
            x_dir = XApp.ScrollDirection(int(direction))

            if direction == Gdk.ScrollDirection.UP:
                delta = -1
            elif direction == Gdk.ScrollDirection.DOWN:
                delta = 1
            elif direction == Gdk.ScrollDirection.LEFT:
                delta = -1
            elif direction == Gdk.ScrollDirection.RIGHT:
                delta = 1

        self.proxy.call_scroll_sync(delta, x_dir, event.time, None)

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
            rx = x
            ry = y

        return rx, ry

class MateXAppStatusApplet(object):
    def __init__(self, applet, iid):
        self.applet = applet
        self.applet.set_flags(MatePanelApplet.AppletFlags.EXPAND_MINOR)
        self.applet.set_can_focus(False)
        self.applet.set_background_widget(self.applet)

        self.add_about()

        button_css = Gtk.CssProvider()

        if button_css.load_from_data(statusicon_css_string.encode()):
            Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), button_css, 600)

        self.applet.connect("realize", self.on_applet_realized)
        self.applet.connect("destroy", self.on_applet_destroy)

        self.indicators = {}
        self.monitor = None

    def add_about(self):
        group = Gtk.ActionGroup(name="xapp-status-applet-group")
        group.set_translation_domain("xapp")

        about_action = Gtk.Action(name="ShowAbout",
                                  icon_name="info",
                                  label=_("About"),
                                  visible=True)

        about_action.connect("activate", self.show_about)
        group.add_action(about_action)

        xml = '\
            <menuitem name="ShowDesktopAboutItem" action="ShowAbout"/> \
        '

        self.applet.setup_menu(xml, group)

    def show_about(self, action, data=None):
        dialog = Gtk.AboutDialog.new()

        dialog.set_program_name("XApp Status Applet")
        dialog.set_version(applet_constants.PKGVERSION)
        dialog.set_license_type(Gtk.License.GPL_3_0)
        dialog.set_website("https://github.com/linuxmint/xapps")
        dialog.set_logo_icon_name("panel-applets")
        dialog.set_comments(_("Area where XApp status icons appear"))

        dialog.run()
        dialog.destroy()

    def on_applet_realized(self, widget, data=None):
        self.indicator_box = Gtk.Box(visible=True)

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

    def make_key(self, proxy):
        name = proxy.get_name()
        path = proxy.get_object_path()

        # print("Key: %s" % (name+path))
        return name + path

    def destroy_monitor (self):
        for key in self.indicators.keys():
            self.indicator_box.remove(self.indicators[key])

        self.monitor = None
        self.indicators = {}

    def on_icon_added(self, monitor, proxy):
        key = self.make_key(proxy)

        self.indicators[key] = StatusWidget(proxy, self.applet.get_orient(), self.applet.get_size())
        self.indicator_box.add(self.indicators[key])
        self.indicators[key].connect("re-sort", self.sort_icons)

        self.sort_icons()

    def on_icon_removed(self, monitor, proxy):
        key = self.make_key(proxy)

        self.indicator_box.remove(self.indicators[key])
        self.indicators[key].disconnect_by_func(self.sort_icons)
        del(self.indicators[key])

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

        if orient in (MatePanelApplet.AppletOrient.LEFT, MatePanelApplet.AppletOrient.RIGHT):
            self.indicator_box.set_orientation(Gtk.Orientation.VERTICAL)

            self.indicator_box.props.margin_start = 0
            self.indicator_box.props.margin_end = 0
            self.indicator_box.props.margin_top = 2
            self.indicator_box.props.margin_bottom = 2
        else:
            self.indicator_box.set_orientation(Gtk.Orientation.HORIZONTAL)

            self.indicator_box.props.margin_start = 2
            self.indicator_box.props.margin_end = 2
            self.indicator_box.props.margin_top = 0
            self.indicator_box.props.margin_bottom = 0

        self.indicator_box.queue_resize()

    def sort_icons(self, status_widget=None):
        icon_list = list(self.indicators.values())

        # for i in icon_list:
        #     print("before: ", i.proxy.props.icon_name, i.proxy.props.name.lower())

        icon_list.sort(key=lambda icon: icon.proxy.props.name.replace("org.x.StatusIcon.", "").lower())
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
