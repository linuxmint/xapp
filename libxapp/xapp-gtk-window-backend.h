#ifndef __XAPP_GTK_WINDOW_BACKEND_H__
#define __XAPP_GTK_WINDOW_BACKEND_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* X11 backend - always built.
 * Each setter is a no-op when the default display is not an X11 display. */
void _xapp_x11_update_icon_name        (GtkWindow   *window,
                                        const gchar *icon_str);
void _xapp_x11_update_progress         (GtkWindow   *window,
                                        guint        progress,
                                        gboolean     pulse);
void _xapp_x11_set_xid_icon_name       (gulong       xid,
                                        const gchar *icon_str);
void _xapp_x11_set_xid_progress        (gulong       xid,
                                        gint         progress);
void _xapp_x11_set_xid_progress_pulse  (gulong       xid,
                                        gboolean     pulse);

#ifdef HAVE_WAYLAND
/* Wayland backend - drives the private xapp-shell protocol.
 * Each setter is a no-op when the window is not a Wayland window or the
 * compositor does not implement xapp-shell. */
void _xapp_wayland_update_icon_name    (GtkWindow   *window,
                                        const gchar *icon_str);
void _xapp_wayland_update_progress     (GtkWindow   *window,
                                        guint        progress,
                                        gboolean     pulse);
void _xapp_wayland_window_unrealized    (GtkWindow   *window);
#endif

G_END_DECLS

#endif /* __XAPP_GTK_WINDOW_BACKEND_H__ */
