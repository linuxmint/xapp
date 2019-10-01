This project gathers the components which are common to multiple desktop environments and required to implement cross-DE solutions.

# xapp-common

A set of resources and tools.

# libxapp

A library available in C, Python and other languages (via Gobject Introspection).

# Scope

In the long term Xapps are there to provide applications to users in multiple DEs, but also to provide building blocks, resources and libraries to Linux developers who want to develop applications which work well in multiple DEs.

This project is still relatively new though and we can't yet commit to a stable ABI.

You are welcome to use libxapp or xapps-common in your application, but if you do, please make sure to tell us, so that we can communicate with you going forward about any potential changes.

# API Reference

http://developer.linuxmint.com/xapps/reference/index.html

## XAppMonitorBlanker

XAppMonitorBlanker is used to blank other monitors. It takes a window as an argument and blanks all the monitors but the one where the window is located.

This is particularly useful in multi-monitor situations, for presentations, full screen media playback etc..

`XAppMonitorBlanker *xapp_monitor_blanker_new (void);`

`void xapp_monitor_blanker_blank_other_monitors (XAppMonitorBlanker *self, GtkWindow   *window);`

`void xapp_monitor_blanker_unblank_monitors (XAppMonitorBlanker *self);`

`gboolean xapp_monitor_blanker_are_monitors_blanked (XAppMonitorBlanker *self);`

This is used by:

- xplayer

## XAppKbdLayoutController

XAppKbdLayoutController is used to get a keyboard layout code or flag for a given locale.

`XAppKbdLayoutController *xapp_kbd_layout_controller_new (void);`

`gboolean xapp_kbd_layout_controller_get_enabled (XAppKbdLayoutController *controller);`

`guint xapp_kbd_layout_controller_get_current_group (XAppKbdLayoutController *controller);`

`void xapp_kbd_layout_controller_set_current_group (XAppKbdLayoutController *controller, guint group);`

`void xapp_kbd_layout_controller_next_group (XAppKbdLayoutController *controller);`

`void xapp_kbd_layout_controller_previous_group (XAppKbdLayoutController *controller);`

`gchar *xapp_kbd_layout_controller_get_current_name (XAppKbdLayoutController *controller);`

`gchar **xapp_kbd_layout_controller_get_all_names (XAppKbdLayoutController *controller);`

`gchar *xapp_kbd_layout_controller_get_current_icon_name (XAppKbdLayoutController *controller);`

`gchar *xapp_kbd_layout_controller_get_icon_name_for_group (XAppKbdLayoutController *controller, guint group);`

`gchar *xapp_kbd_layout_controller_get_short_name (XAppKbdLayoutController *controller);`

`gchar *xapp_kbd_layout_controller_get_short_name_for_group (XAppKbdLayoutController *controller, guint group);`

This is used by:

- cinnamon-screensaver
- cinnamon keyboard applet
