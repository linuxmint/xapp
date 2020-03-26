#!/usr/bin/python3

import locale
import gettext
import os
import sys
import setproctitle

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("XApp", "1.0")
from gi.repository import Gtk, GdkPixbuf, Gdk, GObject, Gio, XApp, GLib

class SnItemWrapper(GObject.Object):
    def __init__(self, sn_item_proxy):
        GObject.Object.__init__(self)

        self.sn_item_proxy = sn_item_proxy
        self.xapp_icon = XApp.StatusIcon()

        self.icon_theme_path = None
        self.xapp_icon.set_name(self.sn_item_proxy.props.id)

        self.sn_item_proxy.connect("new-title", lambda p: self.update_tooltip())
        self.sn_item_proxy.connect("new-icon", lambda p: self.update_icon())
        # self.sn_item_proxy.connect("new-overlay-icon", lambda p: self.update_icon())
        self.sn_item_proxy.connect("new-attention-icon", lambda p: self.update_icon())
        self.sn_item_proxy.connect("new-icon-theme-path", lambda p: self.update_icon())
        # self.sn_item_proxy.connect("new-tooltip", lambda p: self.update_tooltip())
        self.sn_item_proxy.connect("new-status", lambda p: self.update_icon())
        self.sn_item_proxy.connect("new-menu", lambda p: self.update_menu())

        self.xapp_icon.connect("activate", self.on_xapp_icon_activated)

        self.update_icon()
        # self.update_title()
        self.update_tooltip()
        self.update_menu()

    def on_xapp_icon_activated(self, button, time, data=None):
        try:
            self.sn_item_proxy.call_activate_sync(1, 1, None) # does it matter?
        except GLib.Error as e:
            print("why does this happen?", e.message)

    def update_tooltip(self):
        pass
        # This is useless, title gets used like an identifier.  tooltip needs a lot of
        # implementation (icons, html, etc..)

        # self.xapp_icon.set_tooltip_text(self.sn_item_proxy.props.title)

    def update_menu(self):
        #TODO
        pass

    def update_icon(self):
        props = self.sn_item_proxy.props

        # print("IconName: '%s', OverlayIconName: '%s', AttentionIconName: '%s'\n"
        #       "IconPixmap: %d, OverlayIconPixmap: %d, AttentionIconPixmap: %d"
        #           % (props.icon_name, props.overlay_icon_name, props.attention_icon_name,
        #              props.icon_pixmap != None, props.overlay_icon_pixmap != None, props.attention_icon_pixmap != None))

        self.icon_theme_path = props.icon_theme_path
        status = props.status

        if status == "Passive":
            self.xapp_icon.set_visible(False)
            return

        self.xapp_icon.set_visible(True)

        if status == "NeedsAttention":
            if props.attention_icon_name or props.attention_icon_pixmap:
                self.set_icon(props.attention_icon_name,
                              props.attention_icon_pixmap,
                              props.overlay_icon_name,
                              props.ovrelay_icon_pixmap)
                return

        self.set_icon(props.icon_name,
                      props.icon_pixmap,
                      props.overlay_icon_name,
                      props.overlay_icon_pixmap)

    def set_icon(self, primary_name, primary_pixmap, overlay_name, overlay_pixmap):
        if overlay_name or overlay_pixmap:
            self.build_composite_icon(primary_name,
                                      primary_pixmap,
                                      overlay_name,
                                      overlay_pixmap)
            return

        # absolute path provided
        if primary_name:
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

        # TODO: pixmaps

    def build_composite_icon(self, primary_name, primary_pixmap, overlay_name, overlay_pixmap):
        # TODO: bleh
        pass

