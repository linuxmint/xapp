
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include "xapp-status-icon.h"
#include "xapp-status-icon-monitor.h"
#include "xapp-statusicon-interface.h"

#define DEBUG_FLAG XAPP_DEBUG_STATUS_ICON
#include "xapp-debug.h"

#define MONITOR_NAME "org.x.StatusIconMonitor"

#define STATUS_ICON_MATCH "org.x.StatusIcon."
#define STATUS_ICON_INTERFACE "org.x.StatusIcon"

#define STATUS_ICON_PATH "/org/x/StatusIcon"
#define STATUS_ICON_PATH_PREFIX STATUS_ICON_PATH "/"

#define WATCHER_MAX_RESTARTS 2

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

    GHashTable *object_managers;

    guint owner_id;
    guint listener_id;
} XAppStatusIconMonitorPrivate;

struct _XAppStatusIconMonitor
{
    GObject parent_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE (XAppStatusIconMonitor, xapp_status_icon_monitor, G_TYPE_OBJECT)

static void
on_object_manager_name_owner_changed (GObject    *object,
                                      GParamSpec *pspec,
                                      gpointer    user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);
    gchar *name, *owner;

    g_object_get (object,
                  "name-owner", &owner,
                  "name", &name,
                  NULL);

    DEBUG("App name owner changed - name '%s' is now %s)",
            name, owner != NULL ? "owned" : "unowned");

    if (owner == NULL)
    {
        g_hash_table_remove (priv->object_managers, name);
    }

    g_free (owner);
    g_free (name);
}

static void
object_manager_object_added (XAppObjectManagerClient *manager,
                             GDBusObject             *object,
                             gpointer                 user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);
    GDBusInterface *proxy;

    proxy = g_dbus_object_get_interface (object, STATUS_ICON_INTERFACE);

    g_signal_emit (self, signals[ICON_ADDED], 0, XAPP_STATUS_ICON_INTERFACE_PROXY (proxy));

    g_object_unref (proxy);
}

static void
object_manager_object_removed (XAppObjectManagerClient *manager,
                               GDBusObject             *object,
                               gpointer                 user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);
    GDBusInterface *proxy;

    proxy = g_dbus_object_get_interface (object, STATUS_ICON_INTERFACE);

    g_signal_emit (self, signals[ICON_REMOVED], 0, XAPP_STATUS_ICON_INTERFACE_PROXY (proxy));

    g_object_unref (proxy);
}

static void
new_object_manager_created (GObject      *object,
                            GAsyncResult *res,
                            gpointer      user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);
    GDBusObjectManager *obj_mgr;
    GList *objects = NULL, *iter;

    GError *error;
    gchar *name;

    error = NULL;

    obj_mgr = xapp_object_manager_client_new_finish (res,
                                                     &error);

    if (error)
    {
        g_warning ("Couldn't create object manager for bus name: %s", error->message);
        g_error_free (error);
        return;
    }

    g_object_get (obj_mgr, "name", &name, NULL);

    DEBUG("Object manager added for new bus name: '%s'", name);

    g_signal_connect (obj_mgr,
                      "notify::name-owner",
                      G_CALLBACK (on_object_manager_name_owner_changed),
                      self);

    g_signal_connect (obj_mgr,
                      "object-added",
                      G_CALLBACK (object_manager_object_added),
                      self);

    g_signal_connect (obj_mgr,
                      "object-removed",
                      G_CALLBACK (object_manager_object_removed),
                      self);

    g_hash_table_insert (priv->object_managers,
                         name,
                         obj_mgr);

    objects = g_dbus_object_manager_get_objects (obj_mgr);

    for (iter = objects; iter != NULL; iter = iter->next)
    {
        GDBusObject *object = G_DBUS_OBJECT (iter->data);
        GDBusInterface *proxy = g_dbus_object_get_interface (object, STATUS_ICON_INTERFACE);

        g_signal_emit (self, signals[ICON_ADDED], 0, proxy);

        g_object_unref (proxy);
    }

    g_list_free_full (objects, g_object_unref);
}

static void
add_object_manager_for_name (XAppStatusIconMonitor *self,
                             const gchar           *name)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);
    gchar **name_parts = NULL;

    name_parts = g_strsplit (name, ".", -1);

    if (g_strv_length (name_parts) == 4)
    {
        xapp_object_manager_client_new (priv->connection,
                                        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                                        name,
                                        STATUS_ICON_PATH,
                                        NULL,
                                        new_object_manager_created,
                                        self);
    }
    else
    {
        DEBUG ("Adding object manager failed, bus name '%s' is invalid", name);
    }

    g_strfreev (name_parts);
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
            DEBUG ("Found new status icon app: %s", str);
            add_object_manager_for_name (self, str);
        }
    }

    g_variant_iter_free (iter);
    g_variant_unref (result);
}

static void
find_and_add_icons (XAppStatusIconMonitor *self)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    DEBUG("Looking for status icon apps on the bus");

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
add_sn_watcher (XAppStatusIconMonitor *self)
{
    GError *error = NULL;

    if (!g_spawn_command_line_async (XAPP_SN_WATCHER_PATH, &error))
    {
        g_warning ("Could not spawn StatusNotifier watcher (xapp-sn-watcher): %s", error->message);
        g_warning ("Support will be limited to native XAppStatusIcons only");

        g_error_free (error);
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
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);

    const gchar *name;
    const gchar *old_owner;
    const gchar *new_owner;

    DEBUG ("NameOwnerChanged signal received: %s)", sender_name);

    g_variant_get (parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

    if (new_owner[0] != '\0')
    {
        add_object_manager_for_name (self, name);
    }
}

static void
add_name_listener (XAppStatusIconMonitor *self)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    DEBUG ("Adding NameOwnerChanged listener for status icon apps");

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

    DEBUG ("Name owned on bus: %s", name);

    add_name_listener (self);
    find_and_add_icons (self);
    add_sn_watcher (self);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (user_data);
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    DEBUG ("Connected to bus: %s", name);

    priv->connection = connection;
}

static void
connect_to_bus (XAppStatusIconMonitor *self)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);
    gchar *valid_app_name, *owned_name;
    static gint unique_id = 0;

    valid_app_name = g_strdelimit (g_strdup (g_get_prgname ()), ".-,=+~`/", '_');
    owned_name = g_strdup_printf ("%s.%s_%d", MONITOR_NAME, valid_app_name, unique_id++);
    g_free (valid_app_name);

    DEBUG ("Attempting to own name on bus: %s", owned_name);

    priv->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                     owned_name,
                                     G_DBUS_CONNECTION_FLAGS_NONE,
                                     on_bus_acquired,
                                     on_name_acquired,
                                     on_name_lost,
                                     self,
                                     NULL);

    g_free(owned_name);
}

static void
xapp_status_icon_monitor_init (XAppStatusIconMonitor *self)
{
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    priv->object_managers = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, g_object_unref);

    connect_to_bus (self);
}

static void
xapp_status_icon_monitor_dispose (GObject *object)
{
    XAppStatusIconMonitor *self = XAPP_STATUS_ICON_MONITOR (object);
    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (self);

    DEBUG ("XAppStatusIconMonitor dispose (%p)", object);

    if (priv->connection != NULL)
    {
        if (priv->listener_id > 0)
        {
            g_dbus_connection_signal_unsubscribe (priv->connection, priv->listener_id);
            priv->listener_id = 0;
        }

        if (priv->object_managers != NULL)
        {
            g_hash_table_unref (priv->object_managers);
            priv->object_managers = NULL;
        }

        if (priv->owner_id > 0)
        {
            g_bus_unown_name(priv->owner_id);
            priv->owner_id = 0;
        }

        g_clear_object (&priv->connection);
    }

    G_OBJECT_CLASS (xapp_status_icon_monitor_parent_class)->dispose (object);
}

static void
xapp_status_icon_monitor_finalize (GObject *object)
{
    DEBUG ("XAppStatusIconMonitor finalize (%p)", object);

    G_OBJECT_CLASS (xapp_status_icon_monitor_parent_class)->finalize (object);
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

static void
gather_objects_foreach_func (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
    GDBusObjectManager *obj_mgr = G_DBUS_OBJECT_MANAGER (value);
    GList **ret = (GList **) user_data;

    GList *objects = NULL, *iter;

    objects = g_dbus_object_manager_get_objects (obj_mgr);

    for (iter = objects; iter != NULL; iter = iter->next)
    {
        GDBusObject *object = G_DBUS_OBJECT (iter->data);
        GDBusInterface *proxy = g_dbus_object_get_interface (object, STATUS_ICON_INTERFACE);

        *ret = g_list_prepend (*ret, proxy);

        g_object_unref (proxy);
    }

    g_list_free_full (objects, g_object_unref);
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
    GList *ret = NULL;

    XAppStatusIconMonitorPrivate *priv = xapp_status_icon_monitor_get_instance_private (monitor);

    g_hash_table_foreach (priv->object_managers,
                          (GHFunc) gather_objects_foreach_func,
                          &ret);

    return ret;
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

