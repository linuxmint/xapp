
#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "xapp-gtk-window.h"

#define DEBUG_FLAG XAPP_DEBUG_WINDOW
#include "xapp-debug.h"

#define ICON_NAME_HINT "_NET_WM_XAPP_ICON_NAME"
#define PROGRESS_HINT  "_NET_WM_XAPP_PROGRESS"
#define PROGRESS_PULSE_HINT  "_NET_WM_XAPP_PROGRESS_PULSE"

/**
 * SECTION:xapp-gtk-window
 * @Short_description: A subclass of %GtkWindow that allows additional
   communication with the window manager.
 * @Title: XAppGtkWindow
 *
 * This widget is a simple subclass of GtkWindow that provides the following
 * additional capabilities:
 *
 * - Ability to set an icon name or icon file path for the window manager to
 *   make use of, rather than relying on a desktop file or fixed-size window-
 *   backed icon that Gtk normally generates.  The window manager must support
 *   the NET_WM_XAPP_ICON_NAME hint.
 *
 * - Ability to send progress info to the window manager, in the form of an integer,
 *   0-100, which can then be used to display this progress in some manner in a task
 *   manager or window list.  The window manager must support the NET_WM_XAPP_PROGRESS
 *   hint.
 *
 * - Ability to signal a 'pulsing' progress state, of potentially indeterminate value,
 *   in the form of a boolean, which can be passed on to a window list.  The window
 *   manager must support the NET_WM_XAPP_PROGRESS_PULSE hint
 *
 * Wrappers:
 *
 * Also provided are corresponding wrapper functions for normal GtkWindows.
 * They are not class methods - they are called with the target widget as their first
 * argument.
 *
 * For example:
 *
 * win = Gtk.Window()
 * XApp.set_window_icon_name(win, "foobar")
 *
 * These functions mirror those of the #XAppGtkWindow class, but allow the properties
 * to work with normal GtkWindows and descendants of GtkWindow.
 */

typedef struct
{
    gchar   *icon_name;
    gchar   *icon_path;
    guint    progress;
    gboolean progress_pulse;
} XAppGtkWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (XAppGtkWindow, xapp_gtk_window, GTK_TYPE_WINDOW)

static gboolean
is_x11_session (void)
{
    static gboolean running_x11 = FALSE;
    static gsize once_init_value = 0;

    if (g_once_init_enter (&once_init_value))
    {
        running_x11 = GDK_IS_X11_DISPLAY(gdk_display_get_default());
        DEBUG ("XAppGtkWindow: is_x11_session: %s\n", running_x11 ? "TRUE" : "FALSE");

        g_once_init_leave (&once_init_value, 1);
    }

    return running_x11;
}

static void
clear_icon_strings (XAppGtkWindowPrivate *priv)
{
    g_clear_pointer (&priv->icon_name, g_free);
    g_clear_pointer (&priv->icon_path, g_free);
}

static void
set_window_hint_utf8 (Window       xid,
                      const gchar *atom_name,
                      const gchar *str)
{
    GdkDisplay *display;

    display = gdk_display_get_default ();

    if (str != NULL)
    {
        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         xid,
                         gdk_x11_get_xatom_by_name_for_display (display, atom_name),
                         gdk_x11_get_xatom_by_name_for_display (display, "UTF8_STRING"), 8,
                         PropModeReplace, (guchar *) str, strlen (str));
    }
    else
    {
        XDeleteProperty (GDK_DISPLAY_XDISPLAY (display),
                         xid,
                         gdk_x11_get_xatom_by_name_for_display (display, atom_name));
    }
}

static void
set_window_hint_cardinal (Window       xid,
                          const gchar *atom_name,
                          gulong       cardinal)
{
    GdkDisplay *display;

    display = gdk_display_get_default ();

    gdk_error_trap_push ();

    if (cardinal > 0)
    {
        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         xid,
                         gdk_x11_get_xatom_by_name_for_display (display, atom_name),
                         XA_CARDINAL, 32,
                         PropModeReplace,
                         (guchar *) &cardinal, 1);
    }
    else
    {
        XDeleteProperty (GDK_DISPLAY_XDISPLAY (display),
                         xid,
                         gdk_x11_get_xatom_by_name_for_display (display, atom_name));
    }

    gdk_error_trap_pop_ignored ();
}

static Window
get_window_xid (GtkWindow *window)
{
    GdkWindow *gdk_window;

    gdk_window = gtk_widget_get_window (GTK_WIDGET (window));

    if (gdk_window_get_effective_toplevel (gdk_window) != gdk_window)
    {
        g_warning ("Window is not toplevel");
        return 0;
    }

    return GDK_WINDOW_XID (gdk_window);
}

static void
update_window_icon (GtkWindow            *window,
                    XAppGtkWindowPrivate *priv)
{
    if (!is_x11_session ()) {
        return;
    }

    /* Icon name/path */
    if (priv->icon_name != NULL)
    {
        set_window_hint_utf8 (get_window_xid (window),
                              ICON_NAME_HINT,
                              priv->icon_name);
    }
    else if (priv->icon_path != NULL)
    {
        set_window_hint_utf8 (get_window_xid (window),
                              ICON_NAME_HINT,
                              priv->icon_path);
    }
    else
    {
        set_window_hint_utf8 (get_window_xid (window),
                              ICON_NAME_HINT,
                              NULL);
    }
}

static void
update_window_progress (GtkWindow            *window,
                        XAppGtkWindowPrivate *priv)
{
    if (!is_x11_session ()) {
        return;
    }

    /* Progress: 0 - 100 */
    set_window_hint_cardinal (get_window_xid (window),
                              PROGRESS_HINT,
                              (gulong) priv->progress);

    set_window_hint_cardinal (get_window_xid (window),
                              PROGRESS_PULSE_HINT,
                              (gulong) (priv->progress_pulse ? 1 : 0));
}

static void
set_icon_name_internal (GtkWindow            *window,
                        XAppGtkWindowPrivate *priv,
                        const gchar          *icon_name)
{
    if (g_strcmp0 (icon_name, priv->icon_name) == 0)
    {
        gtk_window_set_icon_name (window, icon_name);
        return;
    }

    /* Clear both strings when either is set - this ensures the
     * correct value is set during update_window() */
    clear_icon_strings (priv);

    if (icon_name != NULL)
    {
        priv->icon_name = g_strdup (icon_name);
    }

    /* If the window is realized, set the icon name immediately.
     * If it's not, it will be set by xapp_gtk_window_realize(). */
    if (gtk_widget_get_realized (GTK_WIDGET (window)))
    {
        update_window_icon (window, priv);
    }

    /* Call the GtkWindow method for compatibility */
    gtk_window_set_icon_name (GTK_WINDOW (window), icon_name);
}

static void
set_icon_from_file_internal (GtkWindow            *window,
                             XAppGtkWindowPrivate *priv,
                             const gchar          *file_name,
                             GError              **error)
{
    if (g_strcmp0 (file_name, priv->icon_path) == 0)
    {
        gtk_window_set_icon_from_file (window, file_name, error);
        return;
    }

    /* Clear both strings when either is set - this ensures the correct
     * value is set during update_window() */
    clear_icon_strings (priv);

    if (file_name != NULL)
    {
        priv->icon_path = g_strdup (file_name);
    }

    /* If the window is realized, set the icon path immediately.
     * If it's not, it will be set by xapp_gtk_window_realize(). */
    if (gtk_widget_get_realized (GTK_WIDGET (window)))
    {
        update_window_icon (window, priv);
    }

    gtk_window_set_icon_from_file (GTK_WINDOW (window), file_name, error);
}

static void
set_progress_internal (GtkWindow            *window,
                       XAppGtkWindowPrivate *priv,
                       gint                 progress)
{
    gboolean update;
    guint clamped_progress;

    update = FALSE;

    if (priv->progress_pulse)
    {
        priv->progress_pulse = FALSE;
        update = TRUE;
    }

    clamped_progress = CLAMP (progress, 0, 100);

    if (clamped_progress != priv->progress)
    {
        priv->progress = clamped_progress;
        update = TRUE;
    }

    /* If the window is realized, set the progress immediately.
     * If it's not, it will be set by xapp_gtk_window_realize(). */
    if (gtk_widget_get_realized (GTK_WIDGET (window)))
    {
        if (update)
        {
            update_window_progress (window, priv);
        }
    }
}

static void
set_progress_pulse_internal (GtkWindow            *window,
                             XAppGtkWindowPrivate *priv,
                             gboolean              pulse)
{
    gboolean update;

    update = FALSE;

    if (priv->progress_pulse != pulse)
    {
        priv->progress_pulse = pulse;

        update = TRUE;
    }

    /* If the window is realized, set the progress immediately.
     * If it's not, it will be set by xapp_gtk_window_realize(). */
    if (gtk_widget_get_realized (GTK_WIDGET (window)))
    {
        if (update)
        {
            update_window_progress (window, priv);
        }
    }
}

static void
xapp_gtk_window_realize (GtkWidget *widget)
{
    XAppGtkWindow *window = XAPP_GTK_WINDOW (widget);
    XAppGtkWindowPrivate *priv = xapp_gtk_window_get_instance_private (window);

    GTK_WIDGET_CLASS (xapp_gtk_window_parent_class)->realize (widget);

    update_window_icon (GTK_WINDOW (window), priv);
    update_window_progress (GTK_WINDOW (window), priv);
}

static void
xapp_gtk_window_unrealize (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (xapp_gtk_window_parent_class)->unrealize (widget);
}

static void
xapp_gtk_window_finalize (GObject *object)
{
    XAppGtkWindow *window = XAPP_GTK_WINDOW (object);
    XAppGtkWindowPrivate *priv = xapp_gtk_window_get_instance_private (window);

    clear_icon_strings (priv);

    G_OBJECT_CLASS (xapp_gtk_window_parent_class)->finalize (object);
}

static void
xapp_gtk_window_init (XAppGtkWindow *window)
{
    XAppGtkWindowPrivate *priv;
    priv = xapp_gtk_window_get_instance_private (window);

    priv->icon_name = NULL;
    priv->icon_path = NULL;
}

static void
xapp_gtk_window_class_init (XAppGtkWindowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *wclass = GTK_WIDGET_CLASS (klass);

    gobject_class->finalize = xapp_gtk_window_finalize;
    wclass->realize = xapp_gtk_window_realize;
    wclass->unrealize = xapp_gtk_window_unrealize;
}

/**
 * xapp_gtk_window_new:
 * @type: The #GtkWindowType to use
 *
 * Creates a new #XAppGtkWindow of type @type.  See gtk_window_new()
 * for more details.
 *
 * Returns: A new #XAppGtkWindow (transfer: full)
 */
GtkWidget *
xapp_gtk_window_new (GtkWindowType type)
{
    return g_object_new (XAPP_TYPE_GTK_WINDOW, "type", type, NULL);
}

/**
 * xapp_gtk_window_set_icon_name:
 * @window: The #XAppGtkWindow to set the icon name for
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

    XAppGtkWindowPrivate *priv = xapp_gtk_window_get_instance_private (window);

    set_icon_name_internal (GTK_WINDOW (window), priv, icon_name);
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

    XAppGtkWindowPrivate *priv = xapp_gtk_window_get_instance_private (window);

    set_icon_from_file_internal (GTK_WINDOW (window), priv, file_name, error);
}

/**
 * xapp_gtk_window_set_progress:
 * @window: The #XAppGtkWindow to set the progress for
 * @progress: The value to set for progress.
 *
 * Sets the progress hint for a window manager (like muffin) to make
 * available when applications want to display the application's progress
 * in some operation. The value sent to the WM will be clamped to
 * between 0 and 100.
 *
 * Note: If a window will stick around after progress is complete, you will
 * probaby need to set progress to 0 to remove any progress effects on taskbars
 * and window lists.
 *
 * Setting progress will also cancel the 'pulsing' flag on the window as
 * well, if it has been set.
 */
void
xapp_gtk_window_set_progress (XAppGtkWindow   *window,
                              gint             progress)
{
    g_return_if_fail (XAPP_IS_GTK_WINDOW (window));

    XAppGtkWindowPrivate *priv = xapp_gtk_window_get_instance_private (window);

    set_progress_internal (GTK_WINDOW (window), priv, progress);
}

/**
 * xapp_gtk_window_set_progress_pulse:
 * @window: The #XAppGtkWindow to set the progress for
 * @pulse: Whether to have pulsing set or not.
 * 
 * Sets the progress pulse hint hint for a window manager (like muffin)
 * to make available when applications want to display indeterminate or
 * ongoing progress in a task manager.
 *
 * Note: If a window will stick around after progress is complete, you will
 * probaby need to set progress to 0 to remove any progress effects on taskbars
 * and window lists.  This will also remove the pulse state, if it is set.
 *
 * Setting an explicit progress value will unset this flag.
 */
void
xapp_gtk_window_set_progress_pulse (XAppGtkWindow   *window,
                                    gboolean         pulse)
{
    g_return_if_fail (XAPP_IS_GTK_WINDOW (window));

    XAppGtkWindowPrivate *priv = xapp_gtk_window_get_instance_private (window);

    set_progress_pulse_internal (GTK_WINDOW (window), priv, pulse);
}


/* Wrappers (for GtkWindow subclasses like GtkDialog)
 * window must be a GtkWindow or descendant */
static void
on_gtk_window_realized (GtkWidget *widget,
                        gpointer   user_data)
{
    XAppGtkWindowPrivate *priv;

    priv = (XAppGtkWindowPrivate *) user_data;

    update_window_icon (GTK_WINDOW (widget), priv);
    update_window_progress (GTK_WINDOW (widget), priv);
}

static void
destroy_xapp_struct (gpointer user_data)
{
    XAppGtkWindowPrivate *priv = (XAppGtkWindowPrivate *) user_data;

    g_clear_pointer (&priv->icon_name, g_free);
    g_clear_pointer (&priv->icon_path, g_free);

    g_slice_free (XAppGtkWindowPrivate, priv);
}

static XAppGtkWindowPrivate *
get_xapp_struct (GtkWindow *window)
{
    XAppGtkWindowPrivate *priv;

    priv = g_object_get_data (G_OBJECT (window),
                              "xapp-window-struct");

    if (priv)
    {
        return priv;
    }

    priv = g_slice_new0 (XAppGtkWindowPrivate);

    g_object_set_data_full (G_OBJECT (window),
                            "xapp-window-struct",
                            priv,
                            (GDestroyNotify) destroy_xapp_struct);

    g_signal_connect_after (GTK_WIDGET (window),
                            "realize",
                            G_CALLBACK (on_gtk_window_realized),
                            priv);

    return priv;
}

/**
 * xapp_set_window_icon_name:
 * @window: The #GtkWindow to set the icon name for
 * @icon_name: (nullable): The icon name to set, or %NULL to unset.
 *
 * Sets the icon name hint for a window manager (like muffin) to make
 * available when applications want to change their icons during runtime
 * without having to resort to the internal low-res pixbufs that GdkWindow
 * sets on the client side.  This is a function, not a method, for taking
 * advantage of this feature with descendants of GtkWindows, such as
 * GtkDialogs.  Sets gtk_window_set_icon_name as well, to avoid needing
 * to have two calls each time.  Set to %NULL to unset.
 */
void
xapp_set_window_icon_name (GtkWindow       *window,
                           const gchar     *icon_name)
{
    XAppGtkWindowPrivate *priv;

    g_return_if_fail (GTK_IS_WINDOW (window));

    priv = get_xapp_struct (window);

    if (XAPP_IS_GTK_WINDOW (window))
    {
        g_warning("Window is an instance of XAppGtkWindow.  Use the instance set_icon_name method instead.");
    }

    set_icon_name_internal (window, priv, icon_name);
}


/**
 * xapp_set_window_icon_from_file:
 * @window: The #GtkWindow to set the icon name for
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
xapp_set_window_icon_from_file (GtkWindow   *window,
                                const gchar *file_name,
                                GError     **error)
{
    XAppGtkWindowPrivate *priv;

    g_return_if_fail (GTK_IS_WINDOW (window));

    priv = get_xapp_struct (window);

    if (XAPP_IS_GTK_WINDOW (window))
    {
        g_warning("Window is an instance of XAppGtkWindow.  Use the instance set_icon_from_file method instead.");
    }

    set_icon_from_file_internal (window, priv, file_name, error);
}

/**
 * xapp_set_window_progress:
 * @window: The #GtkWindow to set the progress for
 * @progress: The value to set for progress.
 *
 * Sets the progress hint for a window manager (like muffin) to make
 * available when applications want to display the application's progress
 * in some operation. The value sent to the WM will be clamped to
 * between 0 and 100.
 *
 * Note: If a window will stick around after progress is complete, you will
 * probaby need to set progress to 0 to remove any progress effects on taskbars
 * and window lists.
 *
 * Setting progress will also cancel the 'pulsing' flag on the window as
 * well, if it has been set.
 */
void
xapp_set_window_progress (GtkWindow   *window,
                          gint         progress)
{
    XAppGtkWindowPrivate *priv;

    g_return_if_fail (GTK_IS_WINDOW (window));

    priv = get_xapp_struct (window);

    if (XAPP_IS_GTK_WINDOW (window))
    {
        g_warning("Window is an instance of XAppGtkWindow.  Use the instance set_progress method instead.");
    }

    set_progress_internal (window, priv, progress);
}

/**
 * xapp_set_window_progress_pulse:
 * @window: The #GtkWindow to set the progress for
 * @pulse: Whether to have pulsing set or not.
 * 
 * Sets the progress pulse hint hint for a window manager (like muffin)
 * to make available when applications want to display indeterminate or
 * ongoing progress in a task manager.
 *
 * Note: If a window will stick around after progress is complete, you will
 * probaby need to set progress to 0 to remove any progress effects on taskbars
 * and window lists.  This will also remove the pulse state, if it is set.
 *
 * Setting an explicit progress value will unset this flag.
 */
void
xapp_set_window_progress_pulse (GtkWindow   *window,
                                gboolean     pulse)
{
    XAppGtkWindowPrivate *priv;

    g_return_if_fail (GTK_IS_WINDOW (window));

    priv = get_xapp_struct (window);

    if (XAPP_IS_GTK_WINDOW (window))
    {
        g_warning("Window is an instance of XAppGtkWindow.  Use the instance set_progress_pulse method instead.");
    }

    set_progress_pulse_internal (GTK_WINDOW (window), priv, pulse);
}

/**
 * xapp_set_xid_icon_name:
 * @xid: The Window to set the icon name for
 * @icon_name: (nullable): The icon name to set, or %NULL to unset.
 *
 * Sets the icon name hint for a window manager (like muffin) to make
 * available when applications want to change their icons during runtime
 * without having to resort to the internal low-res pixbufs that GdkWindow
 * sets on the client side.  This is a function, not a method, for applying
 * the icon name property for a given (possibly foreign) window, by passing
 * the window's XID.  Set to %NULL to unset.
 */
void
xapp_set_xid_icon_name (gulong           xid,
                        const gchar     *icon_name)
{
    g_return_if_fail (xid > 0);

    set_window_hint_utf8 (xid, ICON_NAME_HINT, icon_name);
}

/**
 * xapp_set_xid_icon_from_file:
 * @xid: The Window to set the icon name for
 * @file_name: (nullable): The icon path to set, or %NULL to unset.
 *
 * Sets the icon name hint for a window manager (like muffin) to make
 * available when applications want to change their icons during runtime
 * without having to resort to the internal low-res pixbufs that GdkWindow
 * sets on the client side.  This is a function, not a method, for applying
 * the icon name property for a given (possibly foreign) window, by passing
 * the window's XID.  Set to %NULL to unset.
 */
void
xapp_set_xid_icon_from_file (gulong       xid,
                             const gchar *file_name)
{

    g_return_if_fail (xid > 0);

    set_window_hint_utf8 (xid, ICON_NAME_HINT, file_name);
}

/**
 * xapp_set_xid_progress:
 * @xid: The Window to set the progress for
 * @progress: The value to set for progress.
 *
 * Sets the progress hint for a window manager (like muffin) to make
 * available when applications want to display the application's progress
 * in some operation. The value sent to the WM will be clamped to
 * between 0 and 100.
 *
 * Setting progress will also cancel the 'pulsing' flag on the window as
 * well, if it has been set.
 *
 * Note: If a window will stick around after progress is complete, you will
 * probaby need to set progress to 0 to remove any progress effects on taskbars
 * and window lists.
 *
 * This is a function, not a method, for applying the progress property for
 * a given (possibly foreign) window, by passing the window's XID.
 */
void
xapp_set_xid_progress (gulong       xid,
                       gint         progress)
{
    g_return_if_fail (xid > 0);

    set_window_hint_cardinal (xid, PROGRESS_HINT, (gulong) (CLAMP (progress, 0, 100)));
    set_window_hint_cardinal (xid, PROGRESS_PULSE_HINT, (gulong) 0);
}

/**
 * xapp_set_xid_progress_pulse:
 * @xid: The Window to set the progress for
 * @pulse: Whether to have pulsing set or not.
 *
 * Sets the progress pulse hint hint for a window manager (like muffin)
 * to make available when applications want to display indeterminate or
 * ongoing progress in a task manager.
 *
 * Note: If a window will stick around after progress is complete, you will
 * probaby need to set progress to 0 to remove any progress effects on taskbars
 * and window lists.
 *
 * Setting an explicit progress value will unset this flag.
 */
void
xapp_set_xid_progress_pulse (gulong       xid,
                             gboolean     pulse)
{
    g_return_if_fail (xid > 0);

    set_window_hint_cardinal (xid, PROGRESS_PULSE_HINT, (gulong) (pulse ? 1 : 0));
}
