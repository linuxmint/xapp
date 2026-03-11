/* xapp-status-icon-common.c
 *
 * Common utility functions shared across XAppStatusIcon backends
 */

#include <config.h>
#include <gtk/gtk.h>

#include "xapp-status-icon.h"
#include "xapp-status-icon-private.h"

#define DEBUG_FLAG XAPP_DEBUG_STATUS_ICON
#include "xapp-debug.h"

/* Global variables defined in xapp-status-icon.c */
extern guint status_icon_signals[SIGNAL_LAST];
extern XAppStatusIconState process_icon_state;

const gchar *
panel_position_to_str (GtkPositionType type)
{
    switch (type)
    {
        case GTK_POS_LEFT:
            return "Left";
        case GTK_POS_RIGHT:
            return "Right";
        case GTK_POS_TOP:
            return "Top";
        case GTK_POS_BOTTOM:
        default:
            return "Bottom";
    }
}

const gchar *
button_to_str (guint button)
{
    switch (button)
    {
        case GDK_BUTTON_PRIMARY:
            return "Left";
        case GDK_BUTTON_SECONDARY:
            return "Right";
        case GDK_BUTTON_MIDDLE:
            return "Middle";
        default:
            return "Unknown";
    }
}

const gchar *
state_to_str (XAppStatusIconState state)
{
    switch (state)
    {
        case XAPP_STATUS_ICON_STATE_NATIVE:
            return "Native";
        case XAPP_STATUS_ICON_STATE_FALLBACK:
            return "Fallback";
        case XAPP_STATUS_ICON_STATE_NO_SUPPORT:
            return "NoSupport";
        default:
            return "Unknown";
    }
}

const gchar *
scroll_direction_to_str (XAppScrollDirection direction)
{
    switch (direction)
    {
        case XAPP_SCROLL_UP:
            return "Up";
        case XAPP_SCROLL_DOWN:
            return "Down";
        case XAPP_SCROLL_LEFT:
            return "Left";
        case XAPP_SCROLL_RIGHT:
            return "Right";
        default:
            return "Unknown";
    }
}

void
cancellable_reset (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);

    if (priv->cancellable)
    {
        g_cancellable_cancel (priv->cancellable);
        g_object_unref (priv->cancellable);
    }

    priv->cancellable = g_cancellable_new ();
}

static GdkEvent *
synthesize_event (XAppStatusIcon *self,
                 gint            x,
                 gint            y,
                 guint           button,
                 guint           _time,
                 gint            position,
                 GdkWindow     **rect_window,
                 GdkRectangle   *win_rect,
                 GdkGravity     *rect_anchor,
                 GdkGravity     *menu_anchor)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);
    GdkDisplay *display;
    GdkWindow *window;
    GdkSeat *seat;
    GdkWindowAttr attributes;
    gint attributes_mask;
    gint fx, fy;

    display = gdk_display_get_default ();
    seat = gdk_display_get_default_seat (display);

    switch (position)
    {
        case GTK_POS_TOP:
            fx = x;
            fy = y - priv->icon_size;
            *rect_anchor = GDK_GRAVITY_SOUTH_WEST;
            *menu_anchor = GDK_GRAVITY_NORTH_WEST;
            break;
        case GTK_POS_LEFT:
            fx = x - priv->icon_size;
            fy = y;
            *rect_anchor = GDK_GRAVITY_NORTH_EAST;
            *menu_anchor = GDK_GRAVITY_NORTH_WEST;
            break;
        case GTK_POS_RIGHT:
            fx = x;
            fy = y;
            *rect_anchor = GDK_GRAVITY_NORTH_WEST;
            *menu_anchor = GDK_GRAVITY_NORTH_EAST;
            break;
        case GTK_POS_BOTTOM:
        default:
            fx = x;
            fy = y;
            *rect_anchor = GDK_GRAVITY_NORTH_WEST;
            *menu_anchor = GDK_GRAVITY_SOUTH_WEST;
            break;
    }

    attributes.window_type = GDK_WINDOW_CHILD;
    win_rect->x = 0;
    win_rect->y = 0;
    win_rect->width = priv->icon_size;
    win_rect->height = priv->icon_size;
    attributes.x = fx;
    attributes.y = fy;
    attributes.width = priv->icon_size;
    attributes.height = priv->icon_size;
    attributes_mask = GDK_WA_X | GDK_WA_Y;

    window = gdk_window_new (NULL, &attributes, attributes_mask);
    *rect_window = window;

    GdkEvent *event = gdk_event_new (GDK_BUTTON_RELEASE);
    event->any.window = window;
    event->button.device = gdk_seat_get_pointer (seat);

    return event;
}

static void
primary_menu_unmapped (GtkWidget  *widget,
                     gpointer    user_data)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON(user_data));
    XAppStatusIcon *icon = XAPP_STATUS_ICON(user_data);
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("Primary menu unmapped");

    if (process_icon_state == XAPP_STATUS_ICON_STATE_NATIVE && priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_primary_menu_is_open (priv->interface_skeleton, FALSE);
    }

    g_signal_handlers_disconnect_by_func (widget, primary_menu_unmapped, icon);
}

static void
secondary_menu_unmapped (GtkWidget  *widget,
                       gpointer    user_data)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON(user_data));
    XAppStatusIcon *icon = XAPP_STATUS_ICON(user_data);
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("Secondary menu unmapped");

    if (process_icon_state == XAPP_STATUS_ICON_STATE_NATIVE && priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_secondary_menu_is_open (priv->interface_skeleton, FALSE);
    }

    g_signal_handlers_disconnect_by_func (widget, secondary_menu_unmapped, icon);
}

void
popup_menu (XAppStatusIcon *self,
           GtkMenu        *menu,
           gint            x,
           gint            y,
           guint           button,
           guint           _time,
           gint            panel_position)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);
    GdkWindow *rect_window;
    GdkEvent *event;
    GdkRectangle win_rect;
    GdkGravity rect_anchor, menu_anchor;

    DEBUG("Popup menu on behalf of application");

    if (!gtk_widget_get_realized (GTK_WIDGET(menu)))
    {
        GtkWidget *toplevel;
        GtkStyleContext *context;

        gtk_widget_realize (GTK_WIDGET(menu));
        toplevel = gtk_widget_get_toplevel (GTK_WIDGET(menu));
        context = gtk_widget_get_style_context (toplevel);

        /* GtkMenu uses a GtkWindow as its toplevel that is explicitly set to
         * be client-decorated, and applies shadows outside the visible part of
         * the menu. They interfere with clicks on the icon while the menu is open,
         * as the invisible part takes the events instead (and this ends up doing
         * nothing).  It makes the menu a littly ugly, so here's a new class name we
         * can use for themes to restore things bit if we want.  Just avoid shadows. */
        gtk_style_context_remove_class (context, "csd");
        gtk_style_context_add_class (context, "xapp-status-icon-menu-window");
    }

    if (button == GDK_BUTTON_PRIMARY)
    {
        if (process_icon_state == XAPP_STATUS_ICON_STATE_NATIVE && priv->interface_skeleton)
        {
            xapp_status_icon_interface_set_primary_menu_is_open (priv->interface_skeleton, TRUE);
        }

        g_signal_connect (gtk_widget_get_toplevel (GTK_WIDGET(menu)),
                        "unmap",
                        G_CALLBACK(primary_menu_unmapped),
                        self);
    }
    else if (button == GDK_BUTTON_SECONDARY)
    {
        if (process_icon_state == XAPP_STATUS_ICON_STATE_NATIVE && priv->interface_skeleton)
        {
            xapp_status_icon_interface_set_secondary_menu_is_open (priv->interface_skeleton, TRUE);
        }

        g_signal_connect (gtk_widget_get_toplevel (GTK_WIDGET(menu)),
                        "unmap",
                        G_CALLBACK(secondary_menu_unmapped),
                        self);
    }

    event = synthesize_event (self,
                            x, y, button, _time, panel_position,
                            &rect_window, &win_rect, &rect_anchor, &menu_anchor);

    g_object_set_data_full (G_OBJECT(menu),
                          "rect_window", rect_window,
                          (GDestroyNotify) gdk_window_destroy);

    g_object_set (G_OBJECT(menu),
                "anchor-hints", GDK_ANCHOR_SLIDE_X  | GDK_ANCHOR_SLIDE_Y  |
                                GDK_ANCHOR_RESIZE_X | GDK_ANCHOR_RESIZE_Y,
                NULL);

    gtk_menu_popup_at_rect (menu,
                          rect_window,
                          &win_rect,
                          rect_anchor,
                          menu_anchor,
                          event);

    gdk_event_free (event);
}

gboolean
should_send_activate (guint button, gboolean have_button_press)
{
    /* Middle button always activates */
    if (button == GDK_BUTTON_MIDDLE)
    {
        return TRUE;
    }

    /* For primary/secondary, only activate if we saw the button press
     * (this prevents activation when a menu is dismissed by clicking the icon again) */
    return have_button_press;
}

GtkMenu *
get_menu_to_use (XAppStatusIcon *icon, guint button)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);
    GtkWidget *menu_to_use = NULL;

    switch (button)
    {
        case GDK_BUTTON_PRIMARY:
            menu_to_use = priv->primary_menu;
            break;
        case GDK_BUTTON_SECONDARY:
            menu_to_use = priv->secondary_menu;
            break;
    }

    return menu_to_use ? GTK_MENU(menu_to_use) : NULL;
}

/* Signal emission helpers */

void
emit_button_press (XAppStatusIcon *self,
                 gint x, gint y,
                 guint button,
                 guint time,
                 gint panel_position)
{
    g_signal_emit (self, status_icon_signals[SIGNAL_BUTTON_PRESS], 0,
                 x, y, button, time, panel_position);
}

void
emit_button_release (XAppStatusIcon *self,
                   gint x, gint y,
                   guint button,
                   guint time,
                   gint panel_position)
{
    g_signal_emit (self, status_icon_signals[SIGNAL_BUTTON_RELEASE], 0,
                 x, y, button, time, panel_position);
}

void
emit_activate (XAppStatusIcon *self,
             guint button,
             guint time)
{
    g_signal_emit (self, status_icon_signals[SIGNAL_ACTIVATE], 0,
                 button, time);
}

void
emit_scroll (XAppStatusIcon *self,
           gint delta,
           XAppScrollDirection direction,
           guint time)
{
    g_signal_emit (self, status_icon_signals[SIGNAL_SCROLL], 0,
                 delta, direction, time);
}

void
emit_state_changed (XAppStatusIcon *self,
                  XAppStatusIconState state)
{
    g_signal_emit (self, status_icon_signals[SIGNAL_STATE_CHANGED], 0, state);
}

const gchar *
get_process_bus_name (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);
    GDBusConnection *connection = priv->connection;

    if (!connection)
    {
        return NULL;
    }

    return g_dbus_connection_get_unique_name (connection);
}
