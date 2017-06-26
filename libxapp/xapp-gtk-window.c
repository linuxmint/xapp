
#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <X11/Xlib.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "xapp-gtk-window.h"

struct _XAppGtkWindowPrivate
{
    gchar *icon_name;
    gchar *icon_path;
    gboolean need_set_at_realize;
};

G_DEFINE_TYPE (XAppGtkWindow, xapp_gtk_window, GTK_TYPE_WINDOW);

static void
clear_strings (XAppGtkWindow *window)
{
    XAppGtkWindowPrivate *priv = window->priv;

    g_clear_pointer (&priv->icon_name, g_free);
    g_clear_pointer (&priv->icon_path, g_free);
}

static void
set_window_hint (GtkWidget   *widget,
                 const gchar *str)
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

    if (str != NULL)
    {
        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         GDK_WINDOW_XID (window),
                         gdk_x11_get_xatom_by_name_for_display (display, "_NET_WM_XAPP_ICON_NAME"),
                         gdk_x11_get_xatom_by_name_for_display (display, "UTF8_STRING"), 8,
                         PropModeReplace, (guchar *) str, strlen (str));
    }
    else
    {
        XDeleteProperty (GDK_DISPLAY_XDISPLAY (display),
                         GDK_WINDOW_XID (window),
                         gdk_x11_get_xatom_by_name_for_display (display, "_NET_WM_XAPP_ICON_NAME"));
    }
}

static void
update_window (XAppGtkWindow *window)
{
    XAppGtkWindowPrivate *priv = window->priv;

    if (priv->icon_name != NULL)
    {
        set_window_hint (GTK_WIDGET (window), priv->icon_name);
    }
    else if (priv->icon_path != NULL)
    {
        set_window_hint (GTK_WIDGET (window), priv->icon_path);
    }
}

static void
xapp_gtk_window_realize (GtkWidget *widget)
{
    XAppGtkWindow *window = XAPP_GTK_WINDOW (widget);
    XAppGtkWindowPrivate *priv = window->priv;

    GTK_WIDGET_CLASS (xapp_gtk_window_parent_class)->realize (widget);

    if (priv->need_set_at_realize)
    {
        update_window (window);
        priv->need_set_at_realize = FALSE;
    }
}

static void
xapp_gtk_window_unrealize (GtkWidget *widget)
{
    XAppGtkWindow *window = XAPP_GTK_WINDOW (widget);
    XAppGtkWindowPrivate *priv = window->priv;

    GTK_WIDGET_CLASS (xapp_gtk_window_parent_class)->unrealize (widget);

    if (priv->icon_name != NULL || priv->icon_path != NULL)
    {
        priv->need_set_at_realize = TRUE;
    }
}

static void
xapp_gtk_window_finalize (GObject *object)
{
    XAppGtkWindow *window = XAPP_GTK_WINDOW (object);
    XAppGtkWindowPrivate *priv = window->priv;

    g_clear_pointer (&priv->icon_name, g_free);
    g_clear_pointer (&priv->icon_path, g_free);

    G_OBJECT_CLASS (xapp_gtk_window_parent_class)->finalize (object);
}

static void
xapp_gtk_window_init (XAppGtkWindow *window)
{
    XAppGtkWindowPrivate *priv;

    window->priv = G_TYPE_INSTANCE_GET_PRIVATE (window, XAPP_TYPE_GTK_WINDOW, XAppGtkWindowPrivate);
    
    priv = window->priv;

    priv->icon_name = NULL;
    priv->icon_path = NULL;
    priv->need_set_at_realize = FALSE;
}

static void
xapp_gtk_window_class_init (XAppGtkWindowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *wclass = GTK_WIDGET_CLASS (klass);

    gobject_class->finalize = xapp_gtk_window_finalize;
    wclass->realize = xapp_gtk_window_realize;
    wclass->unrealize = xapp_gtk_window_unrealize;

    g_type_class_add_private (gobject_class, sizeof (XAppGtkWindowPrivate));
}

XAppGtkWindow *
xapp_gtk_window_new (void)
{
    return g_object_new (XAPP_TYPE_GTK_WINDOW, NULL);
}

/**
 * xapp_gtk_window_set_icon_name:
 * @window: The #GtkWindow to set the icon name for
 * @icon_name: (nullable): The icon name or path to set, or %NULL to unset.
 * 
 * Sets the icon name hint for a window manager (like muffin) to make
 * available when applications want to change their icons during runtime
 * without having to resort to the internal low-res pixbufs that GdkWindow
 * sets on the client side.  This also chains up and calls GtkWindow.set_icon_name
 * for convenience and compatibility.  Set to %NULL to unset.
 */
void
xapp_gtk_window_set_icon_name (XAppGtkWindow   *window,
                               const gchar     *icon_name)
{
    g_return_if_fail (XAPP_IS_GTK_WINDOW (window));

    clear_strings (window);

    if (icon_name != NULL)
    {
        window->priv->icon_name = g_strdup (icon_name);
    }

    /* If the window is realized, set the icon name immediately */
    if (gtk_widget_get_realized (GTK_WIDGET (window)))
    {
        update_window (window);
    }
    /* Otherwise, remind ourselves to do it in our realize vfunc */
    else
    {
        window->priv->need_set_at_realize = TRUE;
    }

    /* Call the GtkWindow method for compatibility */
    gtk_window_set_icon_name (GTK_WINDOW (window), icon_name);
}


/**
 * xapp_gtk_window_set_icon_from_file:
 * @window: The #XAppGtkWindow to set the icon name for
 * @file_name: (nullable): The icon path to set, or %NULL to unset.
 * @error: (nullable): An error to set if something goes wrong.
 * 
 * Sets the icon name hint for a window manager (like muffin) to make
 * available when applications want to change their icons during runtime
 * without having to resort to the internal low-res pixbufs that GdkWindow
 * sets on the client side.  This also chains up and calls GtkWindow.set_icon_from_file
 * for convenience and compatibility.  Set to %NULL to unset.
 */
void
xapp_gtk_window_set_icon_from_file (XAppGtkWindow   *window,
                                    const gchar     *file_name,
                                    GError         **error)
{
    g_return_if_fail (XAPP_IS_GTK_WINDOW (window));

    clear_strings (window);

    if (file_name != NULL)
    {
        window->priv->icon_path = g_strdup (file_name);
    }

    /* If the window is realized, set the icon path immediately */
    if (gtk_widget_get_realized (GTK_WIDGET (window)))
    {
        update_window (window);
    }
    /* Otherwise, remind ourselves to do it later */
    else
    {
        window->priv->need_set_at_realize = TRUE;
    }

    gtk_window_set_icon_from_file (GTK_WINDOW (window), file_name, error);
}

