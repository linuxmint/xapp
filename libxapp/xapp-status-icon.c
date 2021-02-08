
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include "xapp-status-icon.h"
#include "xapp-statusicon-interface.h"
#include "xapp-enums.h"

#define DEBUG_FLAG XAPP_DEBUG_STATUS_ICON
#include "xapp-debug.h"

#define FDO_DBUS_NAME "org.freedesktop.DBus"
#define FDO_DBUS_PATH "/org/freedesktop/DBus"

#define ICON_BASE_PATH "/org/x/StatusIcon"
#define ICON_SUB_PATH (ICON_BASE_PATH "/Icon")
#define ICON_NAME "org.x.StatusIcon"

#define STATUS_ICON_MONITOR_MATCH "org.x.StatusIconMonitor"

#define MAX_NAME_FAILS 3

#define MAX_SANE_ICON_SIZE 96
#define FALLBACK_ICON_SIZE 24

// This gets reffed and unreffed according to individual icon presence.
// For the first icon, it gets created when exporting the icon's inteface.
// For each additional icon, it gets reffed again. On destruction, the
// opposite occurs - unrefs, and the final unref calls a weak notify
// function, which clears this pointer and unown's the process's bus name.
static GDBusObjectManagerServer *obj_server = NULL;
static guint name_owner_id = 0;

enum
{
    BUTTON_PRESS,
    BUTTON_RELEASE,
    ACTIVATE,
    STATE_CHANGED,
    SCROLL,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

enum
{
    PROP_0,
    PROP_PRIMARY_MENU,
    PROP_SECONDARY_MENU,
    PROP_ICON_SIZE,
    PROP_NAME,
    N_PROPERTIES
};

/**
 * SECTION:xapp-status-icon
 * @Short_description: Broadcasts status information over DBUS
 * @Title: XAppStatusIcon
 *
 * The XAppStatusIcon allows applications to share status info
 * about themselves. It replaces the obsolete and very similar
 * Gtk.StatusIcon widget.
 *
 * If used in an environment where no applet is handling XAppStatusIcons,
 * the XAppStatusIcon delegates its calls to a Gtk.StatusIcon.
 */
typedef struct
{
    GDBusConnection *connection;
    XAppStatusIconInterface *interface_skeleton;
    XAppObjectSkeleton *object_skeleton;

    GCancellable *cancellable;

    GtkStatusIcon *gtk_status_icon;
    GtkWidget *primary_menu;
    GtkWidget *secondary_menu;

    XAppStatusIconState state;

    gchar *name;
    gchar *icon_name;
    gchar *tooltip_text;
    gchar *label;
    gboolean visible;
    gint icon_size;
    gchar *metadata;

    guint listener_id;

    gint fail_counter;
    gboolean have_button_press;
} XAppStatusIconPrivate;

struct _XAppStatusIcon
{
    GObject parent_instance;
    XAppStatusIconPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE (XAppStatusIcon, xapp_status_icon, G_TYPE_OBJECT)

static void refresh_icon        (XAppStatusIcon *self);
static void use_gtk_status_icon (XAppStatusIcon *self);
static void remove_icon_path_from_bus (XAppStatusIcon *self);

static void
cancellable_reset (XAppStatusIcon *self)
{
    if (self->priv->cancellable)
    {
        g_cancellable_cancel (self->priv->cancellable);
        g_object_unref (self->priv->cancellable);
    }

    self->priv->cancellable = g_cancellable_new ();
}

static const gchar *
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

static const gchar *
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

static const gchar *
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

static const gchar *
direction_to_str (XAppScrollDirection direction)
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
        fy = y - self->priv->icon_size;
        *rect_anchor = GDK_GRAVITY_SOUTH_WEST;
        *menu_anchor = GDK_GRAVITY_NORTH_WEST;
        break;
      case GTK_POS_LEFT:
        fx = x - self->priv->icon_size;
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
  win_rect->width = self->priv->icon_size;
  win_rect->height = self->priv->icon_size;
  attributes.x = fx;
  attributes.y = fy;
  attributes.width = self->priv->icon_size;
  attributes.height = self->priv->icon_size;
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
    g_return_if_fail (XAPP_IS_STATUS_ICON (user_data));
    XAppStatusIcon *icon = XAPP_STATUS_ICON (user_data);

    DEBUG ("Primary menu unmapped");

    if (icon->priv->state == XAPP_STATUS_ICON_STATE_NATIVE)
    {
        xapp_status_icon_interface_set_primary_menu_is_open (icon->priv->interface_skeleton, FALSE);
    }

    g_signal_handlers_disconnect_by_func (widget, primary_menu_unmapped, icon);
}

static void
secondary_menu_unmapped (GtkWidget  *widget,
                         gpointer    user_data)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (user_data));
    XAppStatusIcon *icon = XAPP_STATUS_ICON (user_data);

    DEBUG ("Secondary menu unmapped");

    if (icon->priv->state == XAPP_STATUS_ICON_STATE_NATIVE)
    {
        xapp_status_icon_interface_set_secondary_menu_is_open (icon->priv->interface_skeleton, FALSE);
    }

    g_signal_handlers_disconnect_by_func (widget, secondary_menu_unmapped, icon);
}

static void
popup_menu (XAppStatusIcon *self,
            GtkMenu        *menu,
            gint            x,
            gint            y,
            guint           button,
            guint           _time,
            gint            panel_position)
{
    GdkWindow *rect_window;
    GdkEvent *event;
    GdkRectangle win_rect;
    GdkGravity rect_anchor, menu_anchor;

    DEBUG ("Popup menu on behalf of application");

    if (!gtk_widget_get_realized (GTK_WIDGET (menu)))
    {
        GtkWidget *toplevel;
        GtkStyleContext *context;

        gtk_widget_realize (GTK_WIDGET (menu));
        toplevel = gtk_widget_get_toplevel (GTK_WIDGET (menu));
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
        if (self->priv->state == XAPP_STATUS_ICON_STATE_NATIVE)
        {
            xapp_status_icon_interface_set_primary_menu_is_open (self->priv->interface_skeleton, TRUE);
        }

        g_signal_connect (gtk_widget_get_toplevel (GTK_WIDGET (menu)),
                          "unmap",
                          G_CALLBACK (primary_menu_unmapped),
                          self);
    }
    else
    if (button == GDK_BUTTON_SECONDARY)
    {
        if (self->priv->state == XAPP_STATUS_ICON_STATE_NATIVE)
        {
            xapp_status_icon_interface_set_secondary_menu_is_open (self->priv->interface_skeleton, TRUE);
        }

        g_signal_connect (gtk_widget_get_toplevel (GTK_WIDGET (menu)),
                          "unmap",
                          G_CALLBACK (secondary_menu_unmapped),
                          self);
    }

    event = synthesize_event (self,
                              x, y, button, _time, panel_position,
                              &rect_window, &win_rect, &rect_anchor, &menu_anchor);

    g_object_set (G_OBJECT (menu),
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
    gdk_window_destroy (rect_window);
}

static gboolean
should_send_activate (XAppStatusIcon *icon,
                      guint           button)
{
    gboolean do_activate = TRUE;

    switch (button)
    {
        case GDK_BUTTON_PRIMARY:
            if (icon->priv->primary_menu)
            {
                do_activate = FALSE;
            }
            break;
        case GDK_BUTTON_SECONDARY:
            if (icon->priv->secondary_menu)
            {
                do_activate = FALSE;
            }
            break;
        default:
            break;
    }

    return do_activate;
}

static GtkWidget *
get_menu_to_use (XAppStatusIcon *icon,
                 guint           button)
{
    GtkWidget *menu_to_use = NULL;

    switch (button)
    {
        case GDK_BUTTON_PRIMARY:
            menu_to_use = icon->priv->primary_menu;
            break;
        case GDK_BUTTON_SECONDARY:
            menu_to_use = icon->priv->secondary_menu;
            break;
    }

    return menu_to_use;
}

static gboolean
handle_click_method (XAppStatusIconInterface *skeleton,
                     GDBusMethodInvocation   *invocation,
                     gint                     x,
                     gint                     y,
                     guint                    button,
                     guint                    _time,
                     gint                     panel_position,
                     XAppStatusIcon          *icon)
{
    const gchar *name = g_dbus_method_invocation_get_method_name (invocation);

    if (g_strcmp0 (name, "ButtonPress") == 0)
    {
        DEBUG ("Received ButtonPress from monitor %s: "
                 "pos:%d,%d , button: %s , time: %u , orientation: %s",
                 g_dbus_method_invocation_get_sender (invocation),
                 x, y, button_to_str (button), _time, panel_position_to_str (panel_position));

        if (should_send_activate (icon, button))
        {
            DEBUG ("Native sending 'activate' for %s button", button_to_str (button));
            g_signal_emit (icon, signals[ACTIVATE], 0,
                           button,
                           _time);
        }

        icon->priv->have_button_press = TRUE;

        g_signal_emit (icon, signals[BUTTON_PRESS], 0,
                       x, y,
                       button,
                       _time,
                       panel_position);

        xapp_status_icon_interface_complete_button_press (skeleton,
                                                          invocation);
    }
    else
    if (g_strcmp0 (name, "ButtonRelease") == 0)
    {
        DEBUG ("Received ButtonRelease from monitor %s: "
                 "pos:%d,%d , button: %s , time: %u , orientation: %s",
                 g_dbus_method_invocation_get_sender (invocation),
                 x, y, button_to_str (button), _time, panel_position_to_str (panel_position));

        if (icon->priv->have_button_press)
        {
            GtkWidget *menu_to_use = get_menu_to_use (icon, button);

            if (menu_to_use)
            {
                popup_menu (icon,
                            GTK_MENU (menu_to_use),
                            x, y,
                            button,
                            _time,
                            panel_position);
            }

            g_signal_emit (icon, signals[BUTTON_RELEASE], 0,
                           x, y,
                           button,
                           _time,
                           panel_position);
        }

        icon->priv->have_button_press = FALSE;

        xapp_status_icon_interface_complete_button_release (skeleton,
                                                            invocation);
    }

    return TRUE;
}

static gboolean
handle_scroll_method (XAppStatusIconInterface *skeleton,
                      GDBusMethodInvocation   *invocation,
                      gint                     delta,
                      XAppScrollDirection      direction,
                      guint                    _time,
                      XAppStatusIcon          *icon)
{
    DEBUG ("Received Scroll from monitor %s: "
             "delta: %d , direction: %s , time: %u",
             g_dbus_method_invocation_get_sender (invocation),
             delta, direction_to_str (direction), _time);

    g_signal_emit(icon, signals[SCROLL], 0,
                  delta,
                  direction,
                  _time);

    xapp_status_icon_interface_complete_scroll (skeleton,
                                                invocation);

    return TRUE;
}

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

    guint _time;
    guint button;
    gint x, y, orientation;

    button = event->button.button;
    _time = event->button.time;

    DEBUG ("GtkStatusIcon button-press-event with %s button", button_to_str (button));

        /* We always send 'activate' for a button that has no corresponding menu,
         * and for middle clicks. */
    if (should_send_activate (icon, button))
    {
        DEBUG ("GtkStatusIcon activated by %s button", button_to_str (button));

        g_signal_emit (icon, signals[ACTIVATE], 0,
                       button,
                       _time);
    }

    calculate_gtk_status_icon_position_and_orientation (icon,
                                                        status_icon,
                                                        &x,
                                                        &y,
                                                        &orientation);

    icon->priv->have_button_press = TRUE;

    g_signal_emit (icon, signals[BUTTON_PRESS], 0,
                   x, y,
                   button,
                   _time,
                   orientation);

    return GDK_EVENT_PROPAGATE;
}

static void
on_gtk_status_icon_button_release (GtkStatusIcon *status_icon,
                                   GdkEvent      *event,
                                   gpointer       user_data)
{
    XAppStatusIcon *icon = user_data;
    GtkWidget *menu_to_use;
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
                    GTK_MENU (menu_to_use),
                    x,
                    y,
                    button,
                    _time,
                    orientation);
    }

    icon->priv->have_button_press = FALSE;

    g_signal_emit (icon, signals[BUTTON_RELEASE], 0,
                   x, y,
                   button,
                   _time,
                   orientation);
}

static void
name_owner_changed (GDBusConnection *connection,
                    const gchar     *sender_name,
                    const gchar     *object_path,
                    const gchar     *interface_name,
                    const gchar     *signal_name,
                    GVariant        *parameters,
                    gpointer         user_data)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (user_data);
    DEBUG("NameOwnerChanged signal received, refreshing icon");

    refresh_icon (self);
}

static void
add_name_listener (XAppStatusIcon *self)
{
    DEBUG ("Adding NameOwnerChanged listener for status monitors");

    self->priv->listener_id = g_dbus_connection_signal_subscribe (self->priv->connection,
                                                                  FDO_DBUS_NAME,
                                                                  FDO_DBUS_NAME,
                                                                  "NameOwnerChanged",
                                                                  FDO_DBUS_PATH,
                                                                  STATUS_ICON_MONITOR_MATCH,
                                                                  G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE,
                                                                  name_owner_changed,
                                                                  self,
                                                                  NULL);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (user_data);

    g_warning ("XAppStatusIcon: lost or could not acquire presence on dbus.  Refreshing.");

    self->priv->fail_counter++;

    refresh_icon (self);
}

static void
sync_skeleton (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = self->priv;

    priv->fail_counter = 0;

    g_clear_object (&self->priv->gtk_status_icon);

    g_object_set (G_OBJECT (priv->interface_skeleton),
                  "name", priv->name,
                  "label", priv->label,
                  "icon-name", priv->icon_name,
                  "tooltip-text", priv->tooltip_text,
                  "visible", priv->visible,
                  "metadata", priv->metadata,
                  NULL);

    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (priv->interface_skeleton));
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (user_data);

    self->priv->state = XAPP_STATUS_ICON_STATE_NATIVE;

    sync_skeleton (self);

    DEBUG ("Name acquired on dbus, syncing icon properties. State is now: %s",
             state_to_str (self->priv->state));
    g_signal_emit (self, signals[STATE_CHANGED], 0, self->priv->state);
}

typedef struct
{
    const gchar  *signal_name;
    gpointer      callback;
} SkeletonSignal;

static SkeletonSignal skeleton_signals[] = {
    // signal name                                callback
    { "handle-button-press",                      handle_click_method },
    { "handle-button-release",                    handle_click_method },
    { "handle-scroll",                            handle_scroll_method }
};

static void
obj_server_finalized (gpointer  data,
                      GObject  *object)
{
    DEBUG ("Final icon removed, clearing object manager (%s)", g_get_prgname ());

    if (name_owner_id > 0)
    {
        g_bus_unown_name(name_owner_id);
        name_owner_id = 0;
    }

    obj_server = NULL;
}

static void
ensure_object_manager (XAppStatusIcon *self)
{
    if (obj_server == NULL)
    {
        DEBUG ("New object manager for (%s)", g_get_prgname ());

        obj_server = g_dbus_object_manager_server_new (ICON_BASE_PATH);
        g_dbus_object_manager_server_set_connection (obj_server, self->priv->connection);
        g_object_weak_ref (G_OBJECT (obj_server),(GWeakNotify) obj_server_finalized, self);
    }
    else
    {
        g_object_ref (obj_server);
    }
}

static gboolean
export_icon_interface (XAppStatusIcon *self)
{
    gint i;

    ensure_object_manager (self);

    if (self->priv->interface_skeleton)
    {
        return TRUE;
    }

    self->priv->object_skeleton = xapp_object_skeleton_new (ICON_SUB_PATH);
    self->priv->interface_skeleton = xapp_status_icon_interface_skeleton_new ();

    xapp_object_skeleton_set_status_icon_interface (self->priv->object_skeleton,
                                                    self->priv->interface_skeleton);

    g_dbus_object_manager_server_export_uniquely (obj_server,
                                                  G_DBUS_OBJECT_SKELETON (self->priv->object_skeleton));

    g_object_unref (self->priv->object_skeleton);
    g_object_unref (self->priv->interface_skeleton);

    for (i = 0; i < G_N_ELEMENTS (skeleton_signals); i++) {
            SkeletonSignal sig = skeleton_signals[i];

            g_signal_connect (self->priv->interface_skeleton,
                              sig.signal_name,
                              G_CALLBACK (sig.callback),
                              self);
    }

    return TRUE;
}

static void
connect_with_status_applet (XAppStatusIcon *self)
{
    gchar **name_parts = NULL;
    gchar *owner_name;

    name_parts = g_strsplit (self->priv->name, ".", -1);

    if (g_dbus_is_name (self->priv->name) &&
        g_str_has_prefix (self->priv->name, ICON_NAME) &&
        g_strv_length (name_parts) == 4)
    {
        owner_name = g_strdup (self->priv->name);
    }
    else
    {
        gchar *valid_app_name = g_strdelimit (g_strdup (g_get_prgname ()), " .-,=+~`/", '_');

        owner_name = g_strdup_printf ("%s.%s",
                                      ICON_NAME,
                                      valid_app_name);
        g_free (valid_app_name);
    }

    g_strfreev (name_parts);

    if (name_owner_id == 0)
    {
        DEBUG ("Attempting to own name on bus '%s'", owner_name);
        name_owner_id = g_bus_own_name_on_connection (self->priv->connection,
                                                      owner_name,
                                                      G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
                                                      on_name_acquired,
                                                      on_name_lost,
                                                      self,
                                                      NULL);
    }

    g_free(owner_name);
}

static void
update_fallback_icon (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = self->priv;

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

static void
on_gtk_status_icon_embedded_changed (GtkStatusIcon *icon,
                                     GParamSpec    *pspec,
                                     gpointer       user_data)
{
    g_return_if_fail (GTK_IS_STATUS_ICON (icon));

    XAppStatusIcon *self = XAPP_STATUS_ICON (user_data);
    XAppStatusIconPrivate *priv = self->priv;

    if (gtk_status_icon_is_embedded (icon))
    {
        priv->state = XAPP_STATUS_ICON_STATE_FALLBACK;
    }
    else
    {
        priv->state = XAPP_STATUS_ICON_STATE_NO_SUPPORT;
    }

    DEBUG ("Fallback icon embedded_changed. State is now %s",
             state_to_str (priv->state));
    g_signal_emit (self, signals[STATE_CHANGED], 0, priv->state);
}

static void
use_gtk_status_icon (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = self->priv;

    DEBUG ("Falling back to GtkStatusIcon");

    remove_icon_path_from_bus (self);

    // Make sure there wasn't already one
    g_clear_object (&self->priv->gtk_status_icon);

    self->priv->gtk_status_icon = gtk_status_icon_new ();

    g_signal_connect (priv->gtk_status_icon,
                      "button-press-event",
                      G_CALLBACK (on_gtk_status_icon_button_press),
                      self);
    g_signal_connect (priv->gtk_status_icon,
                      "button-release-event",
                      G_CALLBACK (on_gtk_status_icon_button_release),
                      self);
    g_signal_connect (priv->gtk_status_icon,
                      "notify::embedded",
                      G_CALLBACK (on_gtk_status_icon_embedded_changed),
                      self);

    update_fallback_icon (self);
}

static void
on_list_names_completed (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON(user_data);
    GVariant *result;
    GVariantIter *iter;
    gchar *str;
    GError *error;
    gboolean found;

    error = NULL;

    result = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source),
                                            res,
                                            &error);

    if (error != NULL)
    {
        if (error->code != G_IO_ERROR_CANCELLED)
        {
            g_critical ("XAppStatusIcon: attempt to ListNames failed: %s\n", error->message);
            use_gtk_status_icon (self);
        }
        else
        {
            DEBUG ("Attempt to ListNames cancelled");
        }

        g_error_free (error);
        return;
    }

    g_variant_get (result, "(as)", &iter);

    found = FALSE;

    while (g_variant_iter_loop (iter, "s", &str))
    {
        if (g_str_has_prefix (str, STATUS_ICON_MONITOR_MATCH))
        {
            DEBUG ("Discovered active status monitor (%s)", str);
            found = TRUE;
        }
    }

    g_variant_iter_free (iter);
    g_variant_unref (result);

    if (found && export_icon_interface (self))
    {
        if (name_owner_id > 0)
        {
            sync_skeleton (self);
        }
        else
        {
            connect_with_status_applet (self);

            return;
        }
    }
    else
    {
        use_gtk_status_icon (self);
    }
}

static void
look_for_status_applet (XAppStatusIcon *self)
{
    // Check that there is at least one applet on DBUS
    DEBUG("Looking for status monitors");

    cancellable_reset (self);

    g_dbus_connection_call (self->priv->connection,
                            FDO_DBUS_NAME,
                            FDO_DBUS_PATH,
                            FDO_DBUS_NAME,
                            "ListNames",
                            NULL,
                            G_VARIANT_TYPE ("(as)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            3000, /* 3 secs */
                            self->priv->cancellable,
                            on_list_names_completed,
                            self);
}

static void
complete_icon_setup (XAppStatusIcon *self)
{
    if (self->priv->listener_id == 0)
        {
            add_name_listener (self);
        }

    /* There is a potential loop in the g_bus_own_name sequence -
     * if we fail to acquire a name, we refresh again and potentially
     * fail again.  If we fail more than MAX_NAME_FAILS, then quit trying
     * and just use the fallback icon   It's pretty unlikely for*/
    if (self->priv->fail_counter == MAX_NAME_FAILS)
    {
        use_gtk_status_icon (self);
        return;
    }

    look_for_status_applet (self);
}

static void
on_session_bus_connected (GObject      *source,
                          GAsyncResult *res,
                          gpointer      user_data)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (user_data);
    GError *error;

    error = NULL;

    self->priv->connection = g_bus_get_finish (res, &error);

    if (error != NULL)
    {
        if (error->code != G_IO_ERROR_CANCELLED)
        {
            g_critical ("XAppStatusIcon: Unable to acquire session bus: %s", error->message);


            /* If we never get a connection, we use the Gtk icon exclusively, and will never
             * re-try.  FIXME? this is unlikely to happen, so I don't see the point in trying
             * later, as there are probably bigger problems in this case. */
            use_gtk_status_icon (self);
        }
        else
        {
            DEBUG ("Cancelled session bus acquire");
        }

        g_error_free (error);
        return;
    }

    complete_icon_setup (self);
}

static void
refresh_icon (XAppStatusIcon *self)
{
    if (self->priv->connection == NULL)
    {
        DEBUG ("Connecting to session bus");

        cancellable_reset (self);

        g_bus_get (G_BUS_TYPE_SESSION,
                   self->priv->cancellable,
                   on_session_bus_connected,
                   self);
    }
    else
    {
        complete_icon_setup (self);
    }
}

static void
xapp_status_icon_set_property (GObject    *object,
                               guint       prop_id,
                               const       GValue *value,
                               GParamSpec *pspec)
{
    switch (prop_id)
    {
        case PROP_PRIMARY_MENU:
            xapp_status_icon_set_primary_menu (XAPP_STATUS_ICON (object), g_value_get_object (value));
            break;
        case PROP_SECONDARY_MENU:
            xapp_status_icon_set_secondary_menu (XAPP_STATUS_ICON (object), g_value_get_object (value));
            break;
        case PROP_ICON_SIZE:
            XAPP_STATUS_ICON (object)->priv->icon_size = CLAMP (g_value_get_int (value), 0, MAX_SANE_ICON_SIZE);
            break;
        case PROP_NAME:
            {
                const gchar *name = g_value_get_string (value);
                // Can't be null. We set to g_get_prgname() by default.
                if (name == NULL || name[0] == '\0')
                {
                    break;
                }
            }

            xapp_status_icon_set_name (XAPP_STATUS_ICON (object), g_value_get_string (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_status_icon_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    XAppStatusIcon *icon = XAPP_STATUS_ICON (object);

    switch (prop_id)
    {
        case PROP_PRIMARY_MENU:
            g_value_set_object (value, icon->priv->primary_menu);
            break;
        case PROP_SECONDARY_MENU:
            g_value_set_object (value, icon->priv->secondary_menu);
            break;
        case PROP_ICON_SIZE:
            g_value_set_int (value, icon->priv->icon_size);
            break;
        case PROP_NAME:
            g_value_set_string (value, icon->priv->name);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_status_icon_init (XAppStatusIcon *self)
{
    self->priv = xapp_status_icon_get_instance_private (self);

    self->priv->name = g_strdup (g_get_prgname());

    self->priv->state = XAPP_STATUS_ICON_STATE_NO_SUPPORT;
    self->priv->icon_size = FALLBACK_ICON_SIZE;
    self->priv->icon_name = g_strdup (" ");

    DEBUG ("Init: application name: '%s'", self->priv->name);

    // Default to visible (the same behavior as GtkStatusIcon)
    self->priv->visible = TRUE;

    refresh_icon (self);
}

static void
remove_icon_path_from_bus (XAppStatusIcon *self)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (self));

    if (self->priv->object_skeleton)
    {
        const gchar *path;
        path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self->priv->object_skeleton));

        DEBUG ("Removing interface at path '%s'", path);

        g_dbus_object_manager_server_unexport (obj_server, path);
        self->priv->interface_skeleton = NULL;
        self->priv->object_skeleton = NULL;

        g_object_unref (obj_server);
    }
}

static void
xapp_status_icon_dispose (GObject *object)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (object);

    DEBUG ("XAppStatusIcon dispose (%p)", object);

    g_free (self->priv->name);
    g_free (self->priv->icon_name);
    g_free (self->priv->tooltip_text);
    g_free (self->priv->label);
    g_free (self->priv->metadata);

    g_clear_object (&self->priv->cancellable);

    g_clear_object (&self->priv->primary_menu);
    g_clear_object (&self->priv->secondary_menu);

    if (self->priv->gtk_status_icon != NULL)
    {
        g_signal_handlers_disconnect_by_func (self->priv->gtk_status_icon, on_gtk_status_icon_button_press, self);
        g_signal_handlers_disconnect_by_func (self->priv->gtk_status_icon, on_gtk_status_icon_button_release, self);
        g_object_unref (self->priv->gtk_status_icon);
        self->priv->gtk_status_icon = NULL;
    }

    remove_icon_path_from_bus (self);

    if (self->priv->listener_id > 0)
    {
        g_dbus_connection_signal_unsubscribe (self->priv->connection, self->priv->listener_id);
        self->priv->listener_id = 0;
    }

    g_clear_object (&self->priv->connection);

    G_OBJECT_CLASS (xapp_status_icon_parent_class)->dispose (object);
}

static void
xapp_status_icon_finalize (GObject *object)
{
    DEBUG ("XAppStatusIcon finalize (%p)", object);

    g_clear_object (&XAPP_STATUS_ICON (object)->priv->cancellable);

    G_OBJECT_CLASS (xapp_status_icon_parent_class)->finalize (object);
}

static void
xapp_status_icon_class_init (XAppStatusIconClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = xapp_status_icon_dispose;
    gobject_class->finalize = xapp_status_icon_finalize;
    gobject_class->set_property = xapp_status_icon_set_property;
    gobject_class->get_property = xapp_status_icon_get_property;

    /**
     * XAppStatusIcon:primary-menu:
     *
     * A #GtkMenu to use when requested by the remote monitor via a left (or primary) click.
     *
     * When this property is not %NULL, the menu will be automatically positioned and
     * displayed during a primary button release.
     *
     * When this property IS %NULL, the #XAppStatusIcon::activate will be sent for primary
     * button presses.
     *
     * In both cases, the #XAppStatusIcon::button-press-event and #XAppStatusIcon::button-release-events
     * will be fired like normal.
     *
     * Setting this will remove any floating reference to the menu and assume ownership.
     * As a result, it is not necessary to maintain a reference to it in the parent
     * application (or unref it when finished with it - if you wish to replace the menu,
     * simply call this method again with a new menu.
     *
     * The same #GtkMenu widget can be set as both the primary and secondary.
     */
    g_object_class_install_property (gobject_class, PROP_PRIMARY_MENU,
                                     g_param_spec_object ("primary-menu",
                                                          "Status icon primary (left-click) menu",
                                                          "A menu to bring up when the status icon is left-clicked",
                                                           GTK_TYPE_WIDGET,
                                                           G_PARAM_READWRITE));

    /**
     * XAppStatusIcon:secondary-menu:
     *
     * A #GtkMenu to use when requested by the remote monitor via a right (or secondary) click.
     *
     * When this property is not %NULL, the menu will be automatically positioned and
     * displayed during a secondary button release.
     *
     * When this property IS %NULL, the #XAppStatusIcon::activate will be sent for secondary
     * button presses.
     *
     * In both cases, the #XAppStatusIcon::button-press-event and #XAppStatusIcon::button-release-events
     * will be fired like normal.
     *
     * Setting this will remove any floating reference to the menu and assume ownership.
     * As a result, it is not necessary to maintain a reference to it in the parent
     * application (or unref it when finished with it - if you wish to replace the menu,
     * simply call this method again with a new menu.
     *
     * The same #GtkMenu widget can be set as both the primary and secondary.
     */
    g_object_class_install_property (gobject_class, PROP_SECONDARY_MENU,
                                     g_param_spec_object ("secondary-menu",
                                                          "Status icon secondary (right-click) menu",
                                                          "A menu to bring up when the status icon is right-clicked",
                                                           GTK_TYPE_WIDGET,
                                                           G_PARAM_READWRITE));

    /**
     * XAppStatusIcon:icon-size:
     *
     * The icon size that is preferred by icon monitor/host - this is usually a product
     * of some calculation based on the panel size.  It can be used by the client to size
     * an icon to be saved as a file and its path sent to the host.
     *
     * If this value is 0 it has not been set, and its value can be unreliable if the host
     * has multiple #XAppStatusIconMonitors active.
     */
    g_object_class_install_property (gobject_class, PROP_ICON_SIZE,
                                     g_param_spec_int ("icon-size",
                                                       "The icon size the monitor/host prefers",
                                                       "The icon size that should be used, if the client is"
                                                       " supplying absolute icon paths",
                                                       0,
                                                       96,
                                                       0,
                                                       G_PARAM_READWRITE));

    /**
     * XAppStatusIcon:name:
     *
     * The name of the icon for sorting purposes. If this is in the form of 'org.x.StatusIcon.foo`
     * and set immediately upon creation of the icon, it will also attempt to own this dbus name;
     * this can be useful in sandboxed environments where a well-defined name is required. If 
     * additional icons are created, only the name given to the initial one will be used for dbus,
     * though different names can still affect the sort order. This is set to the value of
     * g_get_prgname() if no other name is provided.
     */
    g_object_class_install_property (gobject_class, PROP_NAME,
                                     g_param_spec_string ("name",
                                                          "The name of the icon for sorting purposes.",
                                                          NULL,
                                                          NULL,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    /**
     * XAppStatusIcon::button-press-event:
     * @icon: The #XAppStatusIcon
     * @x: The absolute x position to use for menu positioning
     * @y: The absolute y position to use for menu positioning
     * @button: The button that was pressed
     * @time: The time supplied by the event, or 0
     * @panel_position: The #GtkPositionType to use for menu positioning
     *
     * Gets emitted when there is a button press received from an applet
     */
    signals[BUTTON_PRESS] =
        g_signal_new ("button-press-event",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 5, G_TYPE_INT, G_TYPE_INT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INT);

    /**
     * XAppStatusIcon::button-release-event:
     * @icon: The #XAppStatusIcon
     * @x: The absolute x position to use for menu positioning
     * @y: The absolute y position to use for menu positioning
     * @button: The button that was released
     * @time: The time supplied by the event, or 0
     * @panel_position: The #GtkPositionType to use for menu positioning
     *
     * Gets emitted when there is a button release received from an applet
     */
    signals[BUTTON_RELEASE] =
        g_signal_new ("button-release-event",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 5, G_TYPE_INT, G_TYPE_INT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INT);

    /**
     * XAppStatusIcon::activate:
     * @icon: The #XAppStatusIcon
     * @button: The button that was pressed
     * @time: The time supplied by the event, or 0
     *
     * Gets emitted when the user activates the status icon.  If the XAppStatusIcon:primary-menu or
     * XAppStatusIcon:secondary-menu is not %NULL, this signal is skipped for the respective button
     * presses.  A middle button click will always send this signal when pressed.
     */
    signals [ACTIVATE] =
        g_signal_new ("activate",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

    /**
     * XAppStatusIcon::state-changed:
     * @icon: The #XAppStatusIcon
     * @new_state: The new #XAppStatusIconState of the icon
     *
     * Gets emitted when the state of the icon changes. If you wish
     * to react to changes in how the status icon is being handled
     * (perhaps to alter the menu or other click behavior), you should
     * connect to this - see #XAppStatusIconState for more details.
     */
    signals [STATE_CHANGED] =
        g_signal_new ("state-changed",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1, XAPP_TYPE_STATUS_ICON_STATE);

    /**
     * XAppStatusIcon::scroll-event:
     * @icon: The #XAppStatusIcon
     * @amount: The amount of movement for the scroll event
     * @direction: the #XAppScrollDirection of the scroll event
     * @time: The time supplied by the event, or 0
     *
     * Gets emitted when the user uses the mouse scroll wheel over the status icon.
     * For the most part, amounts will always be 1, unless an applet supports smooth
     * scrolling.  Generally the direction value is most important.
     */
    signals [SCROLL] =
        g_signal_new ("scroll-event",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 3, G_TYPE_INT, XAPP_TYPE_SCROLL_DIRECTION, G_TYPE_UINT);
}

/**
 * xapp_status_icon_set_name:
 * @icon: a #XAppStatusIcon
 * @name: a name (this defaults to the name of the application, if not set)
 *
 * Sets the status icon name. This is not shown to users.
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_name (XAppStatusIcon *icon, const gchar *name)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));

    if (g_strcmp0 (name, icon->priv->name) == 0)
    {
        return;
    }

    if (name == NULL || name[0] == '\0')
    {
        // name can't be null. We set to g_get_prgname() at startup,
        // and the set_property handler silently ignores nulls, but if this
        // is explicit, warn about it.
        g_warning ("Can't set icon the name to null or empty string");
        return;
    }

    g_clear_pointer (&icon->priv->name, g_free);
    icon->priv->name = g_strdup (name);

    DEBUG ("set_name: %s", name);

    if (icon->priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_name (icon->priv->interface_skeleton, name);
    }

    /* Call this directly instead of in the update_fallback_icon() function,
     * as every time this is called, Gtk re-creates the plug for the icon,
     * so the tray thinks one icon has disappeared and a new one appeared,
     * which can cause flicker and undesirable re-ordering of tray items. */
    if (icon->priv->gtk_status_icon != NULL)
    {
        gtk_status_icon_set_name (icon->priv->gtk_status_icon, name);
    }
}

/**
 * xapp_status_icon_set_icon_name:
 * @icon: a #XAppStatusIcon
 * @icon_name: An icon name or absolute path to an icon.
 *
 * Sets the icon name or local path to use.
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_icon_name (XAppStatusIcon *icon, const gchar *icon_name)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));

    if (g_strcmp0 (icon_name, icon->priv->icon_name) == 0)
    {
        return;
    }

    g_clear_pointer (&icon->priv->icon_name, g_free);
    icon->priv->icon_name = g_strdup (icon_name);

    DEBUG ("set_icon_name: %s", icon_name);

    if (icon->priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_icon_name (icon->priv->interface_skeleton, icon_name);
    }

    update_fallback_icon (icon);
}

/**
 * xapp_status_icon_get_icon_size:
 * @icon: a #XAppStatusIcon
 *
 * Returns: The desired icon size - usually set by the host based on panel size.
 * This is not what it's guaranteed to get, and this is really only useful when
 * receiving absolute icon paths from the client app.
 *
 * Since: 1.8
 */
gint
xapp_status_icon_get_icon_size (XAppStatusIcon *icon)
{
    g_return_val_if_fail (XAPP_IS_STATUS_ICON (icon), FALLBACK_ICON_SIZE);

    if (icon->priv->interface_skeleton == NULL)
    {
        DEBUG ("get_icon_size: %d (fallback)", FALLBACK_ICON_SIZE);

        return FALLBACK_ICON_SIZE;
    }

    gint size;

    size = xapp_status_icon_interface_get_icon_size (icon->priv->interface_skeleton);

    DEBUG ("get_icon_size: %d", size);

    return size;
}

/**
 * xapp_status_icon_set_tooltip_text:
 * @icon: a #XAppStatusIcon
 * @tooltip_text: the text to show in the tooltip
 *
 * Sets the tooltip text
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_tooltip_text (XAppStatusIcon *icon, const gchar *tooltip_text)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));

    if (g_strcmp0 (tooltip_text, icon->priv->tooltip_text) == 0)
    {
        return;
    }

    g_clear_pointer (&icon->priv->tooltip_text, g_free);
    icon->priv->tooltip_text = g_strdup (tooltip_text);

    DEBUG ("set_tooltip_text: %s", tooltip_text);

    if (icon->priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_tooltip_text (icon->priv->interface_skeleton, tooltip_text);
    }

    update_fallback_icon (icon);
}

/**
 * xapp_status_icon_set_label:
 * @icon: a #XAppStatusIcon
 * @label: some text
 *
 * Sets a label, shown beside the icon
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_label (XAppStatusIcon *icon, const gchar *label)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));

    if (g_strcmp0 (label, icon->priv->label) == 0)
    {
        return;
    }

    g_clear_pointer (&icon->priv->label, g_free);
    icon->priv->label = g_strdup (label);

    DEBUG ("set_label: '%s'", label);

    if (icon->priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_label (icon->priv->interface_skeleton, label);
    }
}

/**
 * xapp_status_icon_set_visible:
 * @icon: a #XAppStatusIcon
 * @visible: whether or not the status icon should be visible
 *
 * Sets the visibility of the status icon
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_visible (XAppStatusIcon *icon, const gboolean visible)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));

    if (visible == icon->priv->visible)
    {
        return;
    }

    icon->priv->visible = visible;

    DEBUG ("set_visible: %s", visible ? "TRUE" : "FALSE");

    if (icon->priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_visible (icon->priv->interface_skeleton, visible);
    }

    update_fallback_icon (icon);
}

/**
 * xapp_status_icon_get_visible:
 * @icon: an #XAppStatusIcon
 *
 * Returns whether or not the icon should currently be visible.
 *
 * Returns: the current visibility state.

 * Since: 1.8.5
 */
gboolean
xapp_status_icon_get_visible (XAppStatusIcon *icon)
{
    g_return_val_if_fail (XAPP_IS_STATUS_ICON (icon), FALSE);

    DEBUG ("get_visible: %s", icon->priv->visible ? "TRUE" : "FALSE");

    return icon->priv->visible;
}

/**
 * xapp_status_icon_popup_menu:
 * @icon: an #XAppStatusIcon
 * @menu: (nullable): A #GtkMenu to display when the primary mouse button is released.
 * @x: The x anchor position for the menu.
 * @y: The y anchor position for the menu.
 * @button: The button used to initiate this action (or 0)
 * @_time: The event time (or 0)
 * @panel_position: The #GtkPositionType for the position of the icon.
 *
 * Pop up @menu using the positioning arguments. These arguments should be
 * those provided by a #XAppStatusIcon::button-release-event.
 *
 * Since: 1.8.6
 */
void
xapp_status_icon_popup_menu (XAppStatusIcon *icon,
                             GtkMenu        *menu,
                             gint            x,
                             gint            y,
                             guint           button,
                             guint           _time,
                             gint            panel_position)

{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));
    g_return_if_fail (GTK_IS_MENU (menu) || menu == NULL);
    g_return_if_fail (icon->priv->state != XAPP_STATUS_ICON_STATE_NO_SUPPORT);

    popup_menu (icon,
                menu,
                x, y,
                button,
                _time,
                panel_position);
}

/**
 * xapp_status_icon_set_primary_menu:
 * @icon: an #XAppStatusIcon
 * @menu: (nullable): A #GtkMenu to display when the primary mouse button is released.
 *
 * See the #XAppStatusIcon:primary-menu property for details
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_primary_menu (XAppStatusIcon *icon,
                                   GtkMenu        *menu)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));
    g_return_if_fail (GTK_IS_MENU (menu) || menu == NULL);

    if (menu == GTK_MENU (icon->priv->primary_menu))
    {
        return;
    }

    g_clear_object (&icon->priv->primary_menu);

    DEBUG ("set_primary_menu: %p", menu);

    if (menu)
    {
        icon->priv->primary_menu = GTK_WIDGET (g_object_ref_sink (menu));
    }
}

/**
 * xapp_status_icon_get_primary_menu:
 * @icon: an #XAppStatusIcon
 *
 * Returns a pointer to a #GtkMenu that was set previously for the
 * primary mouse button.  If no menu was set, this returns %NULL.
 *
 * Returns: (transfer none): the #GtkMenu or %NULL if none was set.

 * Since: 1.6
 */
GtkWidget *
xapp_status_icon_get_primary_menu (XAppStatusIcon *icon)
{
    g_return_val_if_fail (XAPP_IS_STATUS_ICON (icon), NULL);

    DEBUG ("get_menu: %p", icon->priv->primary_menu);

    return icon->priv->primary_menu;
}

/**
 * xapp_status_icon_set_secondary_menu:
 * @icon: an #XAppStatusIcon
 * @menu: (nullable): A #GtkMenu to display when the primary mouse button is released.
 *
 * See the #XAppStatusIcon:secondary-menu property for details
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_secondary_menu (XAppStatusIcon *icon,
                                     GtkMenu        *menu)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));
    g_return_if_fail (GTK_IS_MENU (menu) || menu == NULL);

    if (menu == GTK_MENU (icon->priv->secondary_menu))
    {
        return;
    }

    g_clear_object (&icon->priv->secondary_menu);

    DEBUG ("set_secondary_menu: %p", menu);

    if (menu)
    {
        icon->priv->secondary_menu = GTK_WIDGET (g_object_ref_sink (menu));
    }
}

/**
 * xapp_status_icon_get_secondary_menu:
 * @icon: an #XAppStatusIcon
 *
 * Returns a pointer to a #GtkMenu that was set previously for the
 * secondary mouse button.  If no menu was set, this returns %NULL.
 *
 * Returns: (transfer none): the #GtkMenu or %NULL if none was set.

 * Since: 1.6
 */
GtkWidget *
xapp_status_icon_get_secondary_menu (XAppStatusIcon *icon)
{
    g_return_val_if_fail (XAPP_IS_STATUS_ICON (icon), NULL);

    DEBUG ("get_menu: %p", icon->priv->secondary_menu);

    return icon->priv->secondary_menu;
}

/**
 * xapp_status_icon_new:
 *
 * Creates a new #XAppStatusIcon instance
 *
 * Returns: (transfer full): a new #XAppStatusIcon. Use g_object_unref when finished.
 *
 * Since: 1.6
 */
XAppStatusIcon *
xapp_status_icon_new (void)
{
    return g_object_new (XAPP_TYPE_STATUS_ICON, NULL);
}

/**
 * xapp_status_icon_new_with_name:
 *
 * Creates a new #XAppStatusIcon instance and sets its name to %name.
 *
 * Returns: (transfer full): a new #XAppStatusIcon. Use g_object_unref when finished.
 *
 * Since: 1.6
 */
XAppStatusIcon *
xapp_status_icon_new_with_name (const gchar *name)
{
    return g_object_new (XAPP_TYPE_STATUS_ICON,
                         "name", name,
                         NULL);
}

/**
 * xapp_status_icon_get_state:
 * @icon: an #XAppStatusIcon
 *
 * Gets the current #XAppStatusIconState of icon. The state is determined by whether
 * the icon is being displayed by an #XAppStatusMonitor client, a fallback tray icon,
 * or not being displayed at all.
 *
 * See #XAppStatusIconState for more details.
 *
 * Returns: the icon's state.
 *
 * Since: 1.6
 */
XAppStatusIconState
xapp_status_icon_get_state (XAppStatusIcon *icon)
{
    g_return_val_if_fail (XAPP_IS_STATUS_ICON (icon), XAPP_STATUS_ICON_STATE_NO_SUPPORT);

    DEBUG ("get_state: %s", state_to_str (icon->priv->state));

    return icon->priv->state;
}

/**
 * xapp_status_icon_set_metadata:
 * @icon: an #XAppStatusIcon
 * @metadata: (nullable): A json-formatted string of key:values.
 *
 * Sets metadata to pass to the icon proxy for an applet's use. Right now this is only so
 * xapp-sn-watcher can tell the applets when the icon is originating from appindicator so panel
 * button 'highlighting' can behave correctly.
 *
 * Since: 1.8.7
 */
void
xapp_status_icon_set_metadata (XAppStatusIcon  *icon,
                               const gchar     *metadata)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));
    gchar *old_meta;

    DEBUG ("set_metadata: '%s'", metadata);

    if (g_strcmp0 (metadata, icon->priv->metadata) == 0)
    {
        return;
    }

    old_meta = icon->priv->metadata;
    icon->priv->metadata = g_strdup (metadata);
    g_free (old_meta);

    if (icon->priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_metadata (icon->priv->interface_skeleton, metadata);
    }
}

/**
 * xapp_status_icon_any_monitors:
 *
 * Looks for the existence of any active #XAppStatusIconMonitors on the bus.
 *
 * Returns: %TRUE if at least one monitor was found.
 *
 * Since: 1.6
 */
gboolean
xapp_status_icon_any_monitors (void)
{
    GDBusConnection *connection;
    GError *error;
    gboolean found;

    DEBUG("Looking for status monitors");

    error = NULL;
    found = FALSE;

    connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

    if (connection)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (connection,
                                              FDO_DBUS_NAME,
                                              FDO_DBUS_PATH,
                                              FDO_DBUS_NAME,
                                              "ListNames",
                                              NULL,
                                              G_VARIANT_TYPE ("(as)"),
                                              G_DBUS_CALL_FLAGS_NONE,
                           /* 10 seconds */   10 * 1000, NULL, &error);

        if (result)
        {
            GVariantIter *iter;
            gchar *str;

            g_variant_get (result, "(as)", &iter);

            found = FALSE;

            while (g_variant_iter_loop (iter, "s", &str))
            {
                if (g_str_has_prefix (str, STATUS_ICON_MONITOR_MATCH))
                {
                    DEBUG ("Discovered active status monitor (%s)", str);

                    found = TRUE;

                    g_free (str);
                    break;
                }
            }

            g_variant_iter_free (iter);
            g_variant_unref (result);
        }

        g_object_unref (connection);
    }

    if (error)
    {
        g_warning ("Unable to check for monitors: %s", error->message);
        g_error_free (error);
    }

    DEBUG ("Monitors found: %s", found ? "TRUE" : "FALSE");

    return found;
}

