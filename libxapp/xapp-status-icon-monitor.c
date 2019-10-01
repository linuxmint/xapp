
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
#include "xapp-status-icon-monitor.h"
#include "xapp-statusicon-interface.h"

#define MONITOR_PATH "/org/x/StatusIconMonitor"
#define MONITOR_NAME "org.x.StatusIconMonitor"

#define STATUS_ICON_MATCH "org.x.StatusIcon."

enum
{
    ICON_ADDED,
    ICON_REMOVED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

/**
 * SECTION:xapp-status-icon-monitor
 * @Short_description: Looks for XAppStatusIcons on DBUS and communicates
 * info to an applet to represent the icons.
 * @Title: XAppStatusIconMonitor
 *
 * The XAppStatusIconMonitor is intended to be utilized by some status applet
 * to display info about an app.
 *
 * The simplest way to use is to make a new instance of this monitor, and connect
 * to the #XAppStatusIconMonitor::icon-added and #XAppStatusIconMonitor::icon-removed signals.
 * The received object for both of these signals is an #XAppStatusIconInterfaceProxy.
 * It represents an application's #XAppStatusIcon, and has properties available for
 * describing the icon name, tooltip, label and visibility.
 *
 * The proxy also provides methods to handle clicks, which can be called by the applet,
 * to request that the app display its menu.
 */
typedef struct
{
    GDBusConnection *connection;

    GHashTable *icons;
    gchar *name;

    guint owner_id;
    guint listener_id;

} XAppStatusIconMonitorPrivate;

struct _XAppStatusIconMonitor
{
    GObject parent_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE (XAppStatusIconMonitor, xapp_status_icon_monitor, G_TYPE_OBJECT)

static void remove_icon (XAppStatusIconMonitor *self, const gchar *name);

static void
on_proxy_name_owner_changed (GObject    *object,
                             GParamSpec *pspec,
                             gpointer    user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);

    gchar *name_owner = NULL;
    gchar *proxy_name = NULL;

    g_object_get (object,
                  "g-name-owner", &name_owner,
                  "g-name", &proxy_name,
                  NULL);

    g_debug("XAppStatusIconMonitor: proxy name owner changed - name owner '%s' is now '%s')", proxy_name, name_owner);

    if (name_owner == NULL)
    {
        remove_icon (self, proxy_name);
    }

    g_free (name_owner);
    g_free (proxy_name);
}

static void
remove_icon (XAppStatusIconMonitor *self,
             const gchar           *name)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);
    XAppStatusIconInterface *proxy;

    proxy = g_hash_table_lookup (priv->icons, name);

    if (proxy)
    {
        g_object_ref (proxy);

        g_signal_handlers_disconnect_by_func (proxy,
                                              on_proxy_name_owner_changed,
                                              self);

        if (g_hash_table_remove (priv->icons, name))
        {
            g_debug("XAppStatusIconMonitor: removing icon: '%s'", name);

            g_signal_emit (self, signals[ICON_REMOVED], 0, proxy);
        }
        else
        {
            g_assert_not_reached ();
        }

        g_object_unref (proxy);
    }
}

static void
new_status_icon_proxy_complete (GObject      *object,
                                GAsyncResult *res,
                                gpointer      user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);
    XAppStatusIconInterface *proxy;
    GError *error;
    gchar *g_name;

    error = NULL;

    proxy = xapp_status_icon_interface_proxy_new_finish (res,
                                                         &error);

    if (error)
    {
        g_warning ("Couldn't add status icon: %s", error->message);
        g_error_free (error);
        return;
    }

    g_signal_connect_object (proxy,
                             "notify::g-name-owner",
                             G_CALLBACK (on_proxy_name_owner_changed),
                             self,
                             0);

    g_object_get (proxy, "g-name", &g_name, NULL);

    g_hash_table_insert (priv->icons,
                         g_name,
                         proxy);

    g_signal_emit (self, signals[ICON_ADDED], 0, proxy);
}

static void
add_icon (XAppStatusIconMonitor *self,
          const gchar           *name)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    g_debug("XAppStatusIconMonitor: adding icon: '%s'", name);

    xapp_status_icon_interface_proxy_new (priv->connection,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          name,
                                          "/org/x/StatusIcon",
                                          NULL,
                                          new_status_icon_proxy_complete,
                                          self);
}

static void
on_list_names_completed (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);
    GVariant *result;
    GVariantIter *iter;
    gchar *str;
    GError *error;

    error = NULL;

    result = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source),
                                            res,
                                            &error);

    if (error != NULL)
    {
      g_critical ("XAppStatusIconMonitor: attempt to ListNames failed: %s\n", error->message);
      g_error_free (error);
      return;
    }

    g_variant_get (result, "(as)", &iter);

    while (g_variant_iter_loop (iter, "s", &str))
    {
        /* the '.' at the end so we don't catch ourselves in this */
        if (g_str_has_prefix (str, STATUS_ICON_MATCH))
        {
            g_debug ("XAppStatusIconMonitor: found icon: %s", str);
            add_icon (self, str);
        }
    }

    g_variant_iter_free (iter);
    g_variant_unref (result);
}

static void
find_and_add_icons (XAppStatusIconMonitor *self)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);


    g_debug("XAppStatusIconMonitor: looking for status icons on the bus");

    /* If there are no monitors (applets) already running when this is set up,
     * this won't find anything.  The XAppStatusIcons will be in fallback mode,
     * and will only attempt to switch back after seeing this monitor appear
     * on the bus, which means they'll show up via our NameOwnerChanged handler.
     * Basically, this will never find anything if we're the first monitor to appear
     * on the bus.
     */

    g_dbus_connection_call (priv->connection,
                            "org.freedesktop.DBus",
                            "/org/freedesktop/DBus",
                            "org.freedesktop.DBus",
                            "ListNames",
                            NULL,
                            G_VARIANT_TYPE ("(as)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            3000, /* 3 secs */
                            NULL,
                            on_list_names_completed,
                            self);
}

static void
status_icon_name_appeared (XAppStatusIconMonitor *self,
                           const gchar           *name,
                           const gchar           *owner)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    if (!g_str_has_prefix (name, STATUS_ICON_MATCH))
    {
        return;
    }

    if (g_hash_table_contains (priv->icons, name))
    {
        return;
    }

    g_debug ("XAppStatusIconMonitor: new icon appeared: %s", name);

    add_icon (self, name);
}

static void
status_icon_name_vanished (XAppStatusIconMonitor *self,
                           const gchar           *name)
{
    if (!g_str_has_prefix (name, STATUS_ICON_MATCH))
    {
        return;
    }

    g_debug ("XAppStatusIconMonitor: icon presence vanished: %s", name);

    remove_icon (self, name);
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
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);

    const gchar *name;
    const gchar *old_owner;
    const gchar *new_owner;

    g_debug("XAppStatusIconMonitor: NameOwnerChanged signal received: %s)", sender_name);

    g_variant_get (parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

    if (old_owner[0] != '\0')
    {
        status_icon_name_vanished (self, name);
    }

    if (new_owner[0] != '\0')
    {
        status_icon_name_appeared (self, name, new_owner);
    }
}


static void
add_name_listener (XAppStatusIconMonitor *self)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    g_debug ("XAppStatusIconMonitor: Adding NameOwnerChanged listener for status icons");

    priv->listener_id = g_dbus_connection_signal_subscribe (priv->connection,
                                                            "org.freedesktop.DBus",
                                                            "org.freedesktop.DBus",
                                                            "NameOwnerChanged",
                                                            "/org/freedesktop/DBus",
                                                            "org.x.StatusIcon",
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
    if (connection == NULL)
    {
        g_warning ("error acquiring session bus");
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);

    g_debug ("XAppStatusIconMonitor: name acquired on dbus");

    add_name_listener (self);
    find_and_add_icons (self);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    g_debug ("XAppStatusIconMonitor: session bus connection acquired");

    priv->connection = connection;
}

static void
connect_to_bus (XAppStatusIconMonitor *self)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    static gint unique_id = 0;

    char *owner_name = g_strdup_printf("%s.PID-%d-%d", MONITOR_NAME, getpid (), unique_id);

    unique_id++;

    g_debug ("XAppStatusIconMonitor: Attempting to acquire presence on dbus as %s", owner_name);

    priv->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                     owner_name,
                                     G_DBUS_CONNECTION_FLAGS_NONE,
                                     on_bus_acquired,
                                     on_name_acquired,
                                     on_name_lost,
                                     self,
                                     NULL);

    g_free(owner_name);
}

static void
xapp_status_icon_monitor_init (XAppStatusIconMonitor *self)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    priv->name = g_strdup_printf("%s", g_get_application_name());

    priv->icons = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, g_object_unref);

    connect_to_bus (self);
}

static void
xapp_status_icon_monitor_dispose (GObject *object)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (object);
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    g_debug ("XAppStatusIconMonitor dispose (%p)", object);

    if (priv->connection != NULL)
    {
        if (priv->listener_id > 0)
        {
            g_dbus_connection_signal_unsubscribe (priv->connection, priv->listener_id);
        }

        if (priv->owner_id > 0)
        {
            g_bus_unown_name(priv->owner_id);
        }

        g_clear_object (&priv->connection);
    }

    g_free (priv->name);
    g_clear_pointer (&priv->icons, g_hash_table_unref);

    G_OBJECT_CLASS (xapp_status_icon_monitor_parent_class)->dispose (object);
}

static void
xapp_status_icon_monitor_finalize (GObject *object)
{
    g_debug ("XAppStatusIconMonitor finalize (%p)", object);

    G_OBJECT_CLASS (xapp_status_icon_monitor_parent_class)->dispose (object);
}

static void
xapp_status_icon_monitor_class_init (XAppStatusIconMonitorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = xapp_status_icon_monitor_dispose;
    gobject_class->finalize = xapp_status_icon_monitor_finalize;

  /**
   * XAppStatusIconMonitor::icon-added:
   * @monitor: the #XAppStatusIconMonitor
   * @proxy: the interface proxy for the #XAppStatusIcon that has been added.
   *
   * This signal is emitted by the monitor when it has discovered a new
   * #XAppStatusIcon on the bus.
   */
    signals[ICON_ADDED] =
        g_signal_new ("icon-added",
                      XAPP_TYPE_STATUS_ICON_MONITOR,
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1, XAPP_TYPE_STATUS_ICON_INTERFACE_PROXY);

  /**
   * XAppStatusIconMonitor::icon-removed:
   * @monitor: the #XAppStatusIconMonitor
   * @proxy: the #XAppStatusIcon proxy that has been removed.
   *
   * This signal is emitted by the monitor when an #XAppStatusIcon has disappeared
   * from the bus.
   */
    signals[ICON_REMOVED] =
        g_signal_new ("icon-removed",
                      XAPP_TYPE_STATUS_ICON_MONITOR,
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1, XAPP_TYPE_STATUS_ICON_INTERFACE_PROXY);
}

/**
 * xapp_status_icon_monitor_list_icons:
 * @monitor: a #XAppStatusIconMonitor
 *
 * List known icon proxies.
 *
 * Returns: (element-type XAppStatusIconMonitor) (transfer container): a #GList of icons
 *
 * Since: 1.6
 */
GList *
xapp_status_icon_monitor_list_icons (XAppStatusIconMonitor *monitor)
{
    g_return_val_if_fail (XAPP_IS_STATUS_ICON_MONITOR (monitor), NULL);

    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (monitor);

    return g_hash_table_get_values (priv->icons);
}

/**
 * xapp_status_icon_monitor_new:
 *
 * Creates a new monitor.
 *
 * Returns: (transfer full): a new #XAppStatusIconMonitor. Use g_object_unref when finished.
 *
 * Since: 1.6
 */
XAppStatusIconMonitor *
xapp_status_icon_monitor_new (void)
{
    return g_object_new (XAPP_TYPE_STATUS_ICON_MONITOR, NULL);
}

