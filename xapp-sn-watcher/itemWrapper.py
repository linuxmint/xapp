#!/usr/bin/python3

import locale
import gettext
import os
import functools
import sys
import setproctitle

import cairo

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("XApp", "1.0")
gi.require_version("DbusmenuGtk3", "0.4")
from gi.repository import Gtk, GdkPixbuf, Gdk, GObject, Gio, XApp, GLib, DbusmenuGtk3

from notifierItem import SnItem

FALLBACK_ICON_SIZE = 24

class SnItemWrapper(GObject.Object):
    def __init__(self, sn_item_proxy):
        GObject.Object.__init__(self)

        self.sn_item = SnItem(sn_item_proxy)

        self.sn_item.connect("ready", lambda p: self.sn_item_ready())
        self.sn_item.connect("update-status", lambda p: self.update_status())
        self.sn_item.connect("update-icon", lambda p: self.update_icon())
        self.sn_item.connect("update-menu", lambda p: self.update_menu())
        self.sn_item.connect("update-tooltip", lambda p: self.update_tooltip())

        self.status = "Passive"
        self.icon_theme_path = None
        self.menu = None
        self.gtk_menu = None

        self.old_png_path = None
        self.png_path = None
        self.current_icon_id = 0

    def sn_item_ready(self):
        self.xapp_icon = XApp.StatusIcon()
        self.xapp_icon.set_name(self.sn_item.id())
        self.xapp_icon.connect("activate", self.on_xapp_icon_activated)
        self.xapp_icon.connect("button-press-event", self.on_xapp_button_pressed)
        self.xapp_icon.connect("button-release-event", self.on_xapp_button_released)
        self.xapp_icon.connect("scroll-event", self.on_xapp_scroll_event)
        self.xapp_icon.connect("state-changed", self.xapp_icon_state_changed);

    def xapp_icon_state_changed(self, state, data=None):
        if state != XApp.StatusIconState.NO_SUPPORT:
            self.update_all()

    def update_all(self):
        self.update_status()
        self.update_icon()
        self.update_menu()
        self.update_tooltip()

    def destroy(self):
        # self.xapp_icon.set_visible(False)
        self.sn_item = None
        self.xapp_icon = None

        # print("item destroyed")

    def on_xapp_icon_activated(self, icon, button, time, data=None):
        pass

    def on_xapp_button_pressed(self, icon, x, y, button, _time, panel_position):
        # Use this for activate so we can pass the x,y coordinates
        self.sn_item.activate(button, x, y)

    def on_xapp_button_released(self, icon, x, y, button, _time, panel_position):
        if not self.gtk_menu:
            self.sn_item.show_context_menu(button, x, y)

    def on_xapp_scroll_event(self, icon, delta, direction, _time):
        o_str = "horizontal" if direction in (XApp.ScrollDirection.LEFT, XApp.ScrollDirection.RIGHT) else "vertical"

        self.sn_item.scroll(delta, o_str)

    def update_menu(self):
        # print("ItemIsMenu: ", self.sn_item.item_is_menu())
        menu_path = self.sn_item.menu()

        if menu_path == None or menu_path == "":
            self.menu = None
            self.gtk_menu = None
            self.xapp_icon.set_secondary_menu(None)
            return

        self.gtk_menu = DbusmenuGtk3.Menu.new(self.sn_item.sn_item_proxy.get_name(), menu_path)

        self.xapp_icon.set_secondary_menu(self.gtk_menu)

    def update_tooltip(self):
        tooltip = self.sn_item.tooltip()

        self.xapp_icon.set_tooltip_text(tooltip)

    def update_status(self):
        # print(self, self.sn_item)
        self.status = self.sn_item.status()

        if self.status == "Passive":
            self.xapp_icon.set_visible(False)
            return

        self.xapp_icon.set_visible(True)

    def update_icon(self):
        i = self.sn_item
        self.icon_theme_path = i.icon_theme_path()

        # print("IconName: '%s', OverlayIconName: '%s', AttentionIconName: '%s', \n"
        #       "IconPixmap: %d, OverlayIconPixmap: %d, AttentionIconPixmap: %d, \n"
        #       "Path: %s, Status: %s"
        #           % (i.icon_name(), i.overlay_icon_name(), i.att_icon_name(),
        #              i.icon_pixmap() != None, i.overlay_icon_pixmap() != None, i.att_icon_pixmap() != None,
        #              i.icon_theme_path(), i.status()))

        if self.status == "NeedsAttention":
            if i.att_icon_name() or i.att_icon_pixmap():
                self.set_icon(i.att_icon_name(),
                              i.att_icon_pixmap(),
                              i.overlay_icon_name(),
                              i.ovrelay_icon_pixmap())
                return

        self.set_icon(i.icon_name(),
                      i.icon_pixmap(),
                      i.overlay_icon_name(),
                      i.overlay_icon_pixmap())

    def set_icon(self, primary_name, primary_pixmap, overlay_name, overlay_pixmap):
        if overlay_name or overlay_pixmap:
            pass # Not worrying about this for now
            self.build_composite_icon(primary_name,
                                      primary_pixmap,
                                      overlay_name,
                                      overlay_pixmap)
            return

        if primary_name:
            # absolute path provided
            if os.path.isabs(primary_name):
                self.xapp_icon.set_icon_name(primary_name)
                return

            # icon name provided, with custom path
            if self.icon_theme_path != None:
                for x in (".svg", ".png"):
                    path = os.path.join(self.icon_theme_path, primary_name + x)
                    if os.path.exists(path):
                        self.xapp_icon.set_icon_name(path)
                        return

            self.xapp_icon.set_icon_name(primary_name)
            return

        if primary_pixmap:
            path = self.create_png_file(primary_pixmap)

            if path:
                self.xapp_icon.set_icon_name(path)
                GLib.timeout_add_seconds(1, self.remove_old_tmpfile)

    def remove_old_tmpfile(self):
        try:
            os.unlink(self.old_png_path)
        except:
            pass

        return GLib.SOURCE_REMOVE

    def create_png_file(self, primary_pixmap):
        best_size = self.xapp_icon.props.icon_size if self.xapp_icon.props.icon_size > 0 else FALLBACK_ICON_SIZE

        # Sort smallest to largest
        def cmp_icon_sizes(a, b):
            area_a = a[0] * a[1]
            area_b = b[0] * b[1]
            return area_a < area_b

        sorted_icons = sorted(primary_pixmap, key=functools.cmp_to_key(cmp_icon_sizes))
        pixmap_to_use = primary_pixmap[0]

        if len(sorted_icons) > 1:
            best_index = 0

            for x in range(0, len(sorted_icons)):
                pixmap = sorted_icons[x]

                width = pixmap[0]
                height = pixmap[1]

                if width <= best_size and height <= best_size:
                    pixmap_to_use = sorted_icons[x]
                    continue
                else:
                    break

        surface = self.surface_from_pixmap_array(pixmap_to_use)

        if surface:
            self.old_png_path = self.png_path
            self.png_path = os.path.join(GLib.get_tmp_dir(), "xapp-tmp-%s-%d.png" % (hash(self), self.get_icon_id()))

            try:
                surface.write_to_png(self.png_path)
            except Exception as e:
                print("Failed to save png of status icon: %s" % e)

            return self.png_path

        return None

    def surface_from_pixmap_array(self, pixmap_array):
        surface = None

        width, height, b = pixmap_array
        rowstride = width * 4 # (argb)

        # convert argb to rgba
        i = 0

        while i < 4 * width * height:
            alpha    = b[i    ]
            b[i    ] = b[i + 1]
            b[i + 1] = b[i + 2]
            b[i + 2] = b[i + 3]
            b[i + 3] = alpha

            i += 4

        pixbuf = GdkPixbuf.Pixbuf.new_from_bytes(GLib.Bytes.new(b),
                                                 GdkPixbuf.Colorspace.RGB,
                                                 True, 8,
                                                 width, height,
                                                 rowstride)

        if pixbuf:
            scale = 1

            sval = GObject.Value(int)
            screen = Gdk.Screen.get_default()

            if screen.get_setting("gdk-window-scaling-factor", sval):
                scale = sval.get_int()

            surface = Gdk.cairo_surface_create_from_pixbuf(pixbuf,
                                                           scale,
                                                           None)

        return surface

    def get_icon_id(self):
        self.current_icon_id = 1 if self.current_icon_id == 0 else 0
        return self.current_icon_id

        # TODO: pixmaps

    def build_composite_icon(self, primary_name, primary_pixmap, overlay_name, overlay_pixmap):
        # TODO: bleh
        pass

