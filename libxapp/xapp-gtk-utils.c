
#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <X11/Xlib.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "xapp-gtk-utils.h"

static void
set_icon_name (GtkWidget   *widget,
               const gchar *icon_name)
{
    GdkDisplay *display;
    GdkWindow *window;

    window = gtk_widget_get_window (widget);

    if (gdk_window_get_effective_toplevel (window) != window)
    {
        g_warning ("Window is not toplevel");
        return;
    }

    display = gdk_window_get_display (window);

    if (icon_name != NULL)
    {
        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         GDK_WINDOW_XID (window),
                         gdk_x11_get_xatom_by_name_for_display (display, "_NET_WM_XAPP_ICON_NAME"),
                         gdk_x11_get_xatom_by_name_for_display (display, "UTF8_STRING"), 8,
                         PropModeReplace, (guchar *)icon_name, strlen (icon_name));
    }
    else
    {
        XDeleteProperty (GDK_DISPLAY_XDISPLAY (display),
                         GDK_WINDOW_XID (window),
                         gdk_x11_get_xatom_by_name_for_display (display, "_NET_WM_XAPP_ICON_NAME"));
    }
}

static void 
on_gtk_window_realized (GtkWidget *widget,
                        gpointer   user_data)
{
    gchar *int_string;

    g_return_if_fail (GTK_IS_WIDGET (widget));

    int_string = (gchar *) user_data;

    g_signal_handlers_disconnect_by_func (widget, on_gtk_window_realized, int_string);
    g_object_weak_unref (G_OBJECT (widget), (GWeakNotify) g_free, int_string);

    set_icon_name (widget, int_string);

    g_free (int_string);
}

/**
 * xapp_gtk_window_set_icon_name:
 * @window: The #GtkWindow to set the icon name for
 * @icon_name: (nullable): The icon name or path to set, or %NULL to unset.
 * 
 * Sets the icon name hint for a window manager (like muffin) to make
 * available when applications want to change their icons during runtime
 * without having to resort to the internal low-res pixbufs that GdkWindow
 * sets on the client side.  This string can be either an icon name
 * corresponding to something in the icon theme, or an absolute path to an
 * icon, or %NULL to unset.
 */
void
xapp_gtk_window_set_icon_name (GtkWindow   *window,
                               const gchar *icon_name)
{
    g_return_if_fail (GTK_IS_WINDOW (window));

    /* If the window is realized, set the icon name immediately */

    if (gtk_widget_get_realized (GTK_WIDGET (window)))
    {
        set_icon_name (GTK_WIDGET (window), icon_name);
    }
    /* Otherwise, hang a callback on window's realize signal and do it then */
    else
    {
        gchar *int_string;

        int_string = g_strdup (icon_name);

        g_signal_connect_after (GTK_WIDGET (window),
                                "realize",
                                G_CALLBACK (on_gtk_window_realized),
                                int_string);

        /* Insurance, in case window gets destroyed without ever being realized */
        g_object_weak_ref (G_OBJECT (window),
                           (GWeakNotify) g_free,
                           int_string);
    }
}


