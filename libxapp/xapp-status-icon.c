
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

// #include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include "xapp-status-icon.h"
#include "xapp-statusicon-interface.h"

#define FDO_DBUS_NAME "org.freedesktop.DBus"
#define FDO_DBUS_PATH "/org/freedesktop/DBus"

#define ICON_PATH "/org/x/StatusIcon"
#define ICON_NAME "org.x.StatusIcon"

#define STATUS_ICON_MONITOR_MATCH "org.x.StatusIconMonitor"

#define MAX_NAME_FAILS 3

enum
{
    BUTTON_PRESS,
    BUTTON_RELEASE,
    ACTIVATE,
    POPUP_MENU,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

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
 * The XAppStatusIcon delegates its calls to a Gtk.StatusIcon.
 */
struct _XAppStatusIconPrivate
{
    XAppStatusIconInterface *skeleton;
    GDBusConnection *connection;

    GCancellable *cancellable;

    GtkStatusIcon *gtk_status_icon;
    GtkWidget *menu;

    gchar *name;
    gchar *icon_name;
    gchar *tooltip_text;
    gchar *label;
    gboolean visible;

    guint owner_id;
    guint listener_id;

    gint fail_counter;
};

G_DEFINE_TYPE (XAppStatusIcon, xapp_status_icon, G_TYPE_OBJECT);

static void refresh_icon        (XAppStatusIcon *self);
static void use_gtk_status_icon (XAppStatusIcon *self);
static void tear_down_dbus      (XAppStatusIcon *self);

static void
cancellable_init (XAppStatusIcon *self)
{
    if (self->priv->cancellable == NULL)
    {
        self->priv->cancellable = g_cancellable_new ();
    }
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
        g_debug ("XAppStatusIcon: received ButtonPress from monitor %s: "
                 "pos:%d,%d , button: %u , time: %u , orientation: %s",
                 g_dbus_method_invocation_get_sender (invocation),
                 x, y, button, _time, panel_position_to_str (panel_position));

        g_signal_emit (icon, signals[BUTTON_PRESS], 0, x, y, button, _time, panel_position);

        xapp_status_icon_interface_complete_button_press (skeleton,
                                                          invocation);
    }
    else
    if (g_strcmp0 (name, "ButtonRelease") == 0)
    {
        g_debug ("XAppStatusIcon: received ButtonRelease from monitor %s: "
                 "pos:%d,%d , button: %u , time: %u , orientation: %s",
                 g_dbus_method_invocation_get_sender (invocation),
                 x, y, button, _time, panel_position_to_str (panel_position));

        g_signal_emit (icon, signals[BUTTON_RELEASE], 0, x, y, button, _time, panel_position);

        xapp_status_icon_interface_complete_button_release (skeleton,
                                                            invocation);
    }

    return TRUE;
}

static void
popup_gtk_status_icon_with_menu (XAppStatusIcon *icon,
                                 GtkStatusIcon  *gtk_status_icon,
                                 guint           button,
                                 guint           activate_time)
{
    /* We have a menu widget we can pass to the gtk positioning function */
    gtk_menu_popup (GTK_MENU (icon->priv->menu),
                    NULL,
                    NULL,
                    gtk_status_icon_position_menu,
                    gtk_status_icon,
                    button,
                    activate_time);
}

#define BUTTON_PRIMARY 1 // gtk2 doesn't have GDK_BUTTON_PRIMARY

static void
on_gtk_status_icon_activate (GtkStatusIcon *status_icon, gpointer user_data)
{
    XAppStatusIcon *icon = user_data;

    g_debug ("XAppStatusIcon: GtkStatusIcon activate");

    if (g_object_get_data (G_OBJECT (icon), "app-indicator"))
    {
        g_debug ("XAppStatusIcon: GtkStatusIcon (app-indicator) left-click popup menu with menu provided");

        popup_gtk_status_icon_with_menu (icon,
                                         status_icon,
                                         BUTTON_PRIMARY,
                                         gtk_get_current_event_time ());
    }
    else
    {
        g_signal_emit (icon, signals[BUTTON_PRESS], 0, 0, 0, BUTTON_PRIMARY, gtk_get_current_event_time (), -1);
    }
}

static void
on_gtk_status_icon_popup_menu (GtkStatusIcon *status_icon, guint button, guint activate_time, gpointer user_data)
{
    XAppStatusIcon *icon = user_data;

    if (icon->priv->menu)
    {
        g_debug ("XAppStatusIcon: GtkStatusIcon popup menu with menu provided");

        popup_gtk_status_icon_with_menu (icon,
                                         status_icon,
                                         button,
                                         activate_time);
    }
    else
    {
        g_debug ("XAppStatusIcon: GtkStatusIcon popup menu with NO menu provided");

        /* No menu provided, do our best to pass along good position info the app or appindicator 
         * (if they're not using patched appindicator) */
        GdkScreen *screen;
        GdkRectangle irect;
        GtkOrientation orientation;
        gint final_x, final_y, final_o;

        if (gtk_status_icon_get_geometry (status_icon,
                                          &screen,
                                          &irect,
                                          &orientation))
        {
            GdkDisplay *display = gdk_screen_get_display (screen);
            GdkRectangle mrect;

#if GTK_MAJOR_VERSION == 2
            gint monitor_idx;

            monitor_idx = gdk_screen_get_monitor_at_point  (screen,
                                                            irect.x + (irect.width / 2),
                                                            irect.y + (irect.height / 2));

            gdk_screen_get_monitor_geometry (screen, monitor_idx, &mrect);
#else
            GdkMonitor *monitor;
            monitor = gdk_display_get_monitor_at_point (display,
                                                        irect.x + (irect.width / 2),
                                                        irect.y + (irect.height / 2));

            gdk_monitor_get_geometry (monitor, &mrect);
#endif
            switch (orientation)
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

            g_signal_emit (icon, signals[BUTTON_RELEASE], 0, final_x, final_y, button, activate_time, final_o);
        }
        else
        {
            g_signal_emit (icon, signals[BUTTON_RELEASE], 0, 0, 0, button, activate_time, -1);
        }
    }
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
    g_debug("XAppStatusIcon: NameOwnerChanged signal received, refreshing icon");

    refresh_icon (self);
}

static void
add_name_listener (XAppStatusIcon *self)
{
    g_debug ("XAppStatusIcon: Adding NameOwnerChanged listener for status monitors");

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

typedef struct
{
    const gchar  *signal_name;
    gpointer      callback;
} SkeletonSignal;

static SkeletonSignal skeleton_signals[] = {
    // signal name                                callback
    { "handle-button-press",                      handle_click_method },
    { "handle-button-release",                    handle_click_method }
};

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
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (user_data);
    XAppStatusIconPrivate *priv = self->priv;

    g_debug ("XAppStatusIcon: name acquired on dbus, syncing icon properties");

    priv->fail_counter = 0;

    g_object_set (G_OBJECT (priv->skeleton),
                  "name", priv->name,
                  "label", priv->label,
                  "icon-name", priv->icon_name,
                  "tooltip-text", priv->tooltip_text,
                  "visible", priv->visible,
                  NULL);

    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (priv->skeleton));
}

static gboolean
export_icon_interface (XAppStatusIcon *self)
{
    GError *error = NULL;
    gint i;

    if (self->priv->skeleton) {
        return TRUE;
    }

    self->priv->skeleton = xapp_status_icon_interface_skeleton_new ();

    g_debug ("XAppStatusIcon: exporting StatusIcon dbus interface");

    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->priv->skeleton),
                                      self->priv->connection,
                                      ICON_PATH,
                                      &error);

    if (error != NULL) {
        g_critical ("XAppStatusIcon: could not export StatusIcon interface: %s", error->message);
        g_error_free (error);

        return FALSE;
    }

    for (i = 0; i < G_N_ELEMENTS (skeleton_signals); i++) {
            SkeletonSignal sig = skeleton_signals[i];

            g_signal_connect (self->priv->skeleton,
                              sig.signal_name,
                              G_CALLBACK (sig.callback),
                              self);
    }

    return TRUE;
}

static void
connect_with_status_applet (XAppStatusIcon *self)
{
    g_clear_object (&self->priv->gtk_status_icon);

    char *owner_name = g_strdup_printf("%s.PID-%d", ICON_NAME, getpid());

    g_debug ("XAppStatusIcon: Attempting to acquire presence on dbus as %s", owner_name);

    self->priv->owner_id = g_bus_own_name_on_connection (self->priv->connection,
                                                         owner_name,
                                                         G_DBUS_CONNECTION_FLAGS_NONE,
                                                         on_name_acquired,
                                                         on_name_lost,
                                                         self,
                                                         NULL);

    g_free(owner_name);
}

static void
update_fallback_icon (XAppStatusIcon *self,
                      const gchar    *icon_name)
{
    if (g_path_is_absolute (icon_name))
    {
        gtk_status_icon_set_from_file (self->priv->gtk_status_icon, icon_name);
    }
    else
    {
        gtk_status_icon_set_from_icon_name (self->priv->gtk_status_icon, icon_name);
    }
}

static void
use_gtk_status_icon (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = self->priv;

    g_debug ("XAppStatusIcon: falling back to GtkStatusIcon");

    tear_down_dbus (self);

    self->priv->gtk_status_icon = gtk_status_icon_new ();

    g_signal_connect (priv->gtk_status_icon, "activate", G_CALLBACK (on_gtk_status_icon_activate), self);
    g_signal_connect (priv->gtk_status_icon, "popup-menu", G_CALLBACK (on_gtk_status_icon_popup_menu), self);

    update_fallback_icon (self, priv->icon_name ? priv->icon_name : "");
    gtk_status_icon_set_tooltip_text (self->priv->gtk_status_icon, priv->tooltip_text);
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
        if (!g_cancellable_is_cancelled (self->priv->cancellable))
        {
            g_critical ("XAppStatusIcon: attempt to ListNames failed: %s\n", error->message);
        }
        else
        {
            g_clear_object (&self->priv->cancellable);
        }

        g_error_free (error);

        use_gtk_status_icon (self);

        return;
    }

    g_variant_get (result, "(as)", &iter);

    found = FALSE;

    while (g_variant_iter_loop (iter, "s", &str))
    {
        if (g_str_has_prefix (str, STATUS_ICON_MONITOR_MATCH))
        {
            g_debug ("XAppStatusIcon: Discovered active status monitor (%s)", str);
            found = TRUE;
        }
    }

    g_variant_iter_free (iter);
    g_variant_unref (result);

    if (found && export_icon_interface (self))
    {
        if (self->priv->owner_id == 0)
        {
            g_clear_object (&self->priv->gtk_status_icon);

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
    g_debug("XAppStatusIcon: Looking for status monitors");

    cancellable_init (self);

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
        if (!g_cancellable_is_cancelled (self->priv->cancellable))
        {
            g_critical ("XAppStatusIcon: Unable to acquire session bus: %s", error->message);
        }
        else
        {
            g_clear_object (&self->priv->cancellable);
        }

        g_error_free (error);

        /* If we never get a connection, we use the Gtk icon exclusively, and will never
         * re-try.  FIXME? this is unlikely to happen, so I don't see the point in trying
         * later, as there are probably bigger problems in this case. */
        use_gtk_status_icon (self);

        return;
    }

    if (self->priv->connection)
    {
        complete_icon_setup (self);

        return;
    }

    use_gtk_status_icon (self);
}

static void
refresh_icon (XAppStatusIcon *self)
{
    if (!self->priv->connection)
    {
        g_debug ("XAppStatusIcon: Trying to acquire session bus connection");

        self->priv->cancellable = g_cancellable_new ();

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
xapp_status_icon_init (XAppStatusIcon *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, XAPP_TYPE_STATUS_ICON, XAppStatusIconPrivate);
    self->priv->name = g_strdup_printf("%s", g_get_application_name());

    // Default to visible (the same behavior as GtkStatusIcon)
    self->priv->visible = TRUE;

    refresh_icon (self);
}

static void
tear_down_dbus (XAppStatusIcon *self)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (self));

    if (self->priv->owner_id > 0)
    {
        g_debug ("XAppStatusIcon: removing dbus presence (%p)", self);

        g_bus_unown_name(self->priv->owner_id);
        self->priv->owner_id = 0;
    }

    if (self->priv->skeleton)
    {
        g_debug ("XAppStatusIcon: removing dbus interface (%p)", self);

        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->priv->skeleton));
        g_object_unref (self->priv->skeleton);
        self->priv->skeleton = NULL;
    }
}

static void
xapp_status_icon_dispose (GObject *object)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (object);

    g_debug ("XAppStatusIcon dispose (%p)", object);

    g_free (self->priv->name);
    g_free (self->priv->icon_name);
    g_free (self->priv->tooltip_text);
    g_free (self->priv->label);

    g_clear_pointer (&self->priv->cancellable, g_cancellable_cancel);

    if (self->priv->gtk_status_icon != NULL)
    {
        g_signal_handlers_disconnect_by_func (self->priv->gtk_status_icon, on_gtk_status_icon_activate, self);
        g_signal_handlers_disconnect_by_func (self->priv->gtk_status_icon, on_gtk_status_icon_popup_menu, self);
        g_object_unref (self->priv->gtk_status_icon);
    }

    tear_down_dbus (self);

    if (self->priv->listener_id > 0)
    {
        g_dbus_connection_signal_unsubscribe (self->priv->connection, self->priv->listener_id);
    }

    g_clear_object (&self->priv->connection);

    G_OBJECT_CLASS (xapp_status_icon_parent_class)->dispose (object);
}

static void
xapp_status_icon_finalize (GObject *object)
{
    g_debug ("XAppStatusIcon finalize (%p)", object);

    g_clear_object (&XAPP_STATUS_ICON (object)->priv->cancellable);

    G_OBJECT_CLASS (xapp_status_icon_parent_class)->finalize (object);
}

static void
xapp_status_icon_class_init (XAppStatusIconClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = xapp_status_icon_dispose;
    gobject_class->finalize = xapp_status_icon_finalize;

    g_type_class_add_private (gobject_class, sizeof (XAppStatusIconPrivate));

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
   *
   * Gets emitted when the user activates the status icon. 
   */
    signals [ACTIVATE] =
        g_signal_new ("activate",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE,
                      0);

  /**
   * XAppStatusIcon::popup-menu:
   * @icon: the #XAppStatusIcon
   * @button: the button that was pressed, or 0 if the 
   *   signal is not emitted in response to a button press event
   * @activate_time: the timestamp of the event that
   *   triggered the signal emission
   *
   * Gets emitted when the user brings up the context menu
   * of the status icon.
   *
   */
    signals [POPUP_MENU] =
        g_signal_new ("popup-menu",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE,
                      2,
                      G_TYPE_UINT,
                      G_TYPE_UINT);
}

/**
 * xapp_status_icon_set_name:
 * @icon: a #XAppStatusIcon
 * @name: a name (this defaults to the name of the application, if not set)
 *
 * Sets the status icon name. This is now shown to users.
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_name (XAppStatusIcon *icon, const gchar *name)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));
    g_clear_pointer (&icon->priv->name, g_free);
    icon->priv->name = g_strdup (name);

    g_debug ("XAppStatusIcon set_name: %s", name);

    if (icon->priv->skeleton)
    {
        xapp_status_icon_interface_set_name (icon->priv->skeleton, name);
    }

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
 * Sets the icon name or icon filename to use.
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_icon_name (XAppStatusIcon *icon, const gchar *icon_name)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));
    g_clear_pointer (&icon->priv->icon_name, g_free);
    icon->priv->icon_name = g_strdup (icon_name);

    g_debug ("XAppStatusIcon set_icon_name: %s", icon_name);

    if (icon->priv->skeleton)
    {
        xapp_status_icon_interface_set_icon_name (icon->priv->skeleton, icon_name);
    }

    if (icon->priv->gtk_status_icon != NULL)
    {
        update_fallback_icon (icon, icon_name);
    }
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
    g_clear_pointer (&icon->priv->tooltip_text, g_free);
    icon->priv->tooltip_text = g_strdup (tooltip_text);

    g_debug ("XAppStatusIcon set_tooltip_text: %s", tooltip_text);

    if (icon->priv->skeleton)
    {
        xapp_status_icon_interface_set_tooltip_text (icon->priv->skeleton, tooltip_text);
    }

    if (icon->priv->gtk_status_icon != NULL)
    {
        gtk_status_icon_set_tooltip_text (icon->priv->gtk_status_icon, tooltip_text);
    }
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
    g_clear_pointer (&icon->priv->label, g_free);
    icon->priv->label = g_strdup (label);

    g_debug ("XAppStatusIcon set_label: '%s'", label);

    if (icon->priv->skeleton)
    {
        xapp_status_icon_interface_set_label (icon->priv->skeleton, label);
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
    icon->priv->visible = visible;

    g_debug ("XAppStatusIcon set_visible: %s", visible ? "TRUE" : "FALSE");

    if (icon->priv->skeleton)
    {
        xapp_status_icon_interface_set_visible (icon->priv->skeleton, visible);
    }

    if (icon->priv->gtk_status_icon != NULL)
    {
        gtk_status_icon_set_visible (icon->priv->gtk_status_icon, visible);
    }
}

/**
 * xapp_status_icon_set_menu:
 * @icon: a #XAppStatusIcon
 * @menu: A #GtkMenu to display when requested
 *
 * Sets a menu that will be used when requested.
 *
 * Since: 1.6
 */
void
xapp_status_icon_set_menu (XAppStatusIcon *icon,
                           GtkMenu        *menu)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (icon));

    g_clear_object (&icon->priv->menu);
    icon->priv->menu = GTK_WIDGET (g_object_ref (menu));
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
