/* xapp-status-icon-fallback.c
 *
 * Fallback backend for XAppStatusIcon (GtkStatusIcon)
 * Uses legacy X11 xembed system tray protocol
 */

#include <config.h>
#include <gtk/gtk.h>

#include "xapp-status-icon.h"
#include "xapp-status-icon-private.h"
#include "xapp-status-icon-backend.h"

#define DEBUG_FLAG XAPP_DEBUG_STATUS_ICON
#include "xapp-debug.h"

/* Global variables from main file */
extern guint status_icon_signals[];
extern XAppStatusIconState process_icon_state;

/* Forward declarations */
static void calculate_gtk_status_icon_position_and_orientation (XAppStatusIcon *icon,
                                                               GtkStatusIcon  *status_icon,
                                                               gint *x, gint *y, gint *orientation);
static void update_fallback_icon (XAppStatusIcon *self);

/* GtkStatusIcon event handlers */

static void
calculate_gtk_status_icon_position_and_orientation (XAppStatusIcon *icon,
                                                    GtkStatusIcon  *status_icon,
                                                    gint *x, gint *y, gint *orientation)
{
    GdkScreen *screen;
    GdkRectangle irect;
    GtkOrientation iorientation;
    gint final_x, final_y, final_o;

    final_x = 0;
    final_y = 0;
    final_o = 0;

    if (gtk_status_icon_get_geometry (status_icon,
                                      &screen,
                                      &irect,
                                      &iorientation))
    {
        GdkDisplay *display = gdk_screen_get_display (screen);
        GdkMonitor *monitor;
        GdkRectangle mrect;

        monitor = gdk_display_get_monitor_at_point (display,
                                                    irect.x + (irect.width / 2),
                                                    irect.y + (irect.height / 2));

        gdk_monitor_get_workarea (monitor, &mrect);

        switch (iorientation)
        {
            case GTK_ORIENTATION_HORIZONTAL:
                final_x = irect.x;

                if (irect.y + irect.height + 100 < mrect.y + mrect.height)
                {
                    final_y = irect.y + irect.height;
                    final_o = GTK_POS_TOP;
                }
                else
                {
                    final_y = irect.y;
                    final_o = GTK_POS_BOTTOM;
                }

                break;
            case GTK_ORIENTATION_VERTICAL:
                final_y = irect.y;

                if (irect.x + irect.width + 100 < mrect.x + mrect.width)
                {
                    final_x = irect.x + irect.width;
                    final_o = GTK_POS_LEFT;
                }
                else
                {
                    final_x = irect.x;
                    final_o = GTK_POS_RIGHT;
                }
        }
    }

    *x = final_x;
    *y = final_y;
    *orientation = final_o;
}

static gboolean
on_gtk_status_icon_button_press (GtkStatusIcon *status_icon,
                                 GdkEvent      *event,
                                 gpointer       user_data)
{
    XAppStatusIcon *icon = user_data;
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    guint _time;
    guint button;
    gint x, y, orientation;

    button = event->button.button;
    _time = event->button.time;

    DEBUG ("GtkStatusIcon button-press-event with %s button", button_to_str (button));

    /* We always send 'activate' for a button that has no corresponding menu,
     * and for middle clicks. */
    if (should_send_activate (button, priv->have_button_press))
    {
        DEBUG ("GtkStatusIcon activated by %s button", button_to_str (button));

        emit_activate (icon, button, _time);
    }

    calculate_gtk_status_icon_position_and_orientation (icon,
                                                        status_icon,
                                                        &x,
                                                        &y,
                                                        &orientation);

    priv->have_button_press = TRUE;

    emit_button_press (icon, x, y, button, _time, orientation);

    return GDK_EVENT_PROPAGATE;
}

static gboolean
on_gtk_status_icon_button_release (GtkStatusIcon *status_icon,
                                   GdkEvent      *event,
                                   gpointer       user_data)
{
    XAppStatusIcon *icon = user_data;
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);
    GtkMenu *menu_to_use;
    guint _time;
    guint button;
    gint x, y, orientation;

    button = event->button.button;
    _time = event->button.time;

    DEBUG ("GtkStatusIcon button-release-event with %s button", button_to_str (button));

    /* Native icons can have two menus, so we must determine which to use based
     * on the gtk icon event's button. */

    menu_to_use = get_menu_to_use (icon, button);

    calculate_gtk_status_icon_position_and_orientation (icon,
                                                        status_icon,
                                                        &x,
                                                        &y,
                                                        &orientation);

    if (menu_to_use)
    {
        DEBUG ("GtkStatusIcon popup menu for %s button", button_to_str (button));

        popup_menu (icon,
                    menu_to_use,
                    x,
                    y,
                    button,
                    _time,
                    orientation);
    }

    priv->have_button_press = FALSE;

    emit_button_release (icon, x, y, button, _time, orientation);

    return GDK_EVENT_PROPAGATE;
}

static gboolean
on_gtk_status_icon_scroll (GtkStatusIcon *status_icon,
                           GdkEvent      *event,
                           gpointer       user_data)
{
    XAppStatusIcon *icon = user_data;
    guint _time;

    _time = event->scroll.time;
    GdkScrollDirection direction;

    if (gdk_event_get_scroll_direction (event, &direction))
    {
        XAppScrollDirection x_dir = XAPP_SCROLL_UP;
        gint delta = 0;

        if (direction != GDK_SCROLL_SMOOTH) {
            if (direction == GDK_SCROLL_UP)
            {
                x_dir = XAPP_SCROLL_UP;
                delta = -1;
            }
            else if (direction == GDK_SCROLL_DOWN)
            {
                x_dir = XAPP_SCROLL_DOWN;
                delta = 1;
            }
            else if (direction == GDK_SCROLL_LEFT)
            {
                x_dir = XAPP_SCROLL_LEFT;
                delta = -1;
            }
            else if (direction == GDK_SCROLL_RIGHT)
            {
                x_dir = XAPP_SCROLL_RIGHT;
                delta = 1;
            }
        }

        DEBUG ("Received Scroll from GtkStatusIcon %s: "
               "delta: %d , direction: %s , time: %u",
               gtk_status_icon_get_title (status_icon),
               delta, scroll_direction_to_str (direction), _time);

        emit_scroll (icon, delta, x_dir, _time);
    }

    return GDK_EVENT_PROPAGATE;
}

static void
on_gtk_status_icon_embedded_changed (GtkStatusIcon *gtk_icon,
                                     GParamSpec    *pspec,
                                     gpointer       user_data)
{
    g_return_if_fail (GTK_IS_STATUS_ICON (gtk_icon));

    XAppStatusIcon *self = XAPP_STATUS_ICON (user_data);

    if (gtk_status_icon_is_embedded (gtk_icon))
    {
        process_icon_state = XAPP_STATUS_ICON_STATE_FALLBACK;
    }
    else
    {
        process_icon_state = XAPP_STATUS_ICON_STATE_NO_SUPPORT;
    }

    DEBUG ("Fallback icon embedded_changed. State is now %s",
             state_to_str (process_icon_state));
    emit_state_changed (self, process_icon_state);
}

static void
update_fallback_icon (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);

    if (!priv->gtk_status_icon)
    {
        return;
    }

    gtk_status_icon_set_tooltip_text (priv->gtk_status_icon, priv->tooltip_text);

    if (priv->icon_name)
    {
        gtk_status_icon_set_visible (priv->gtk_status_icon, priv->visible);

        if (g_path_is_absolute (priv->icon_name))
        {
            gtk_status_icon_set_from_file (priv->gtk_status_icon, priv->icon_name);
        }
        else
        {
            gtk_status_icon_set_from_icon_name (priv->gtk_status_icon, priv->icon_name);
        }
    }
    else
    {
        gtk_status_icon_set_visible (priv->gtk_status_icon, FALSE);
    }
}

/* Backend operations implementations */

gboolean
fallback_backend_init (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("Fallback backend init - creating GtkStatusIcon");

    process_icon_state = XAPP_STATUS_ICON_STATE_NO_SUPPORT;

    if (priv->gtk_status_icon != NULL)
    {
        return TRUE;
    }

    priv->gtk_status_icon = gtk_status_icon_new ();

    g_signal_connect (priv->gtk_status_icon,
                      "button-press-event",
                      G_CALLBACK (on_gtk_status_icon_button_press),
                      icon);

    g_signal_connect (priv->gtk_status_icon,
                      "button-release-event",
                      G_CALLBACK (on_gtk_status_icon_button_release),
                      icon);

    g_signal_connect (priv->gtk_status_icon,
                      "scroll-event",
                      G_CALLBACK (on_gtk_status_icon_scroll),
                      icon);

    g_signal_connect (priv->gtk_status_icon,
                      "notify::embedded",
                      G_CALLBACK (on_gtk_status_icon_embedded_changed),
                      icon);

    update_fallback_icon (icon);

    emit_state_changed (icon, process_icon_state);

    return TRUE;
}

void
fallback_backend_cleanup (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("Fallback backend cleanup");

    if (priv->gtk_status_icon)
    {
        g_signal_handlers_disconnect_by_data (priv->gtk_status_icon, icon);
        g_clear_object (&priv->gtk_status_icon);
    }
}

void
fallback_backend_sync (XAppStatusIcon *icon)
{
    DEBUG("Fallback backend sync");
    update_fallback_icon (icon);
}

void
fallback_backend_set_icon_name (XAppStatusIcon *icon, const gchar *icon_name)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (priv->gtk_status_icon)
    {
        if (icon_name && g_path_is_absolute (icon_name))
        {
            gtk_status_icon_set_from_file (priv->gtk_status_icon, icon_name);
        }
        else if (icon_name)
        {
            gtk_status_icon_set_from_icon_name (priv->gtk_status_icon, icon_name);
        }

        gtk_status_icon_set_visible (priv->gtk_status_icon, priv->visible && icon_name != NULL);
    }
}

void
fallback_backend_set_tooltip (XAppStatusIcon *icon, const gchar *tooltip)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (priv->gtk_status_icon)
    {
        gtk_status_icon_set_tooltip_text (priv->gtk_status_icon, tooltip);
    }
}

void
fallback_backend_set_visible (XAppStatusIcon *icon, gboolean visible)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (priv->gtk_status_icon)
    {
        gtk_status_icon_set_visible (priv->gtk_status_icon, visible && priv->icon_name != NULL);
    }
}

void
fallback_backend_set_label (XAppStatusIcon *icon, const gchar *label)
{
    /* GtkStatusIcon doesn't support labels - this is a no-op */
    DEBUG("Fallback backend set_label called (GtkStatusIcon doesn't support labels): %s",
          label ? label : "(null)");
}

/* Backend operations structure */
XAppBackend fallback_backend_ops = {
    .type = XAPP_BACKEND_FALLBACK,
    .init = fallback_backend_init,
    .cleanup = fallback_backend_cleanup,
    .sync = fallback_backend_sync,
    .set_icon_name = fallback_backend_set_icon_name,
    .set_tooltip = fallback_backend_set_tooltip,
    .set_visible = fallback_backend_set_visible,
    .set_label = fallback_backend_set_label,
};
