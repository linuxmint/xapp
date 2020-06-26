#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include <libxapp/xapp-status-icon.h>
#include <glib-unix.h>

#include "sn-watcher-interface.h"
#include "sn-item-interface.h"
#include "sn-item.h"

#define XAPP_TYPE_SN_WATCHER xapp_sn_watcher_get_type ()
G_DECLARE_FINAL_TYPE (XAppSnWatcher, xapp_sn_watcher, XAPP, SN_WATCHER, GtkApplication)

struct _XAppSnWatcher
{
    GtkApplication parent_instance;

    SnWatcherInterface *skeleton;
    GDBusConnection *connection;

    guint owner_id;
    guint name_listener_id;

    GHashTable *items;

    gboolean shutdown_pending;
    gboolean advertise_host;
};

G_DEFINE_TYPE (XAppSnWatcher, xapp_sn_watcher, GTK_TYPE_APPLICATION)

#define NOTIFICATION_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define NOTIFICATION_WATCHER_PATH "/StatusNotifierWatcher"
#define STATUS_ICON_MONITOR_PREFIX "org.x.StatusIconMonitor"

#define FDO_DBUS_NAME "org.freedesktop.DBus"
#define FDO_DBUS_PATH "/org/freedesktop/DBus"

#define STATUS_ICON_MONITOR_MATCH "org.x.StatusIconMonitor"
#define APPINDICATOR_PATH_PREFIX "/org/ayatana/NotificationItem/"

GSettings *xapp_settings;

static void continue_startup (XAppSnWatcher *watcher);
static void update_published_items (XAppSnWatcher *watcher);

static void
handle_status_applet_name_owner_appeared (XAppSnWatcher *watcher,
                                          const gchar   *name,
                                          const gchar   *new_owner)
{
    if (g_str_has_prefix (name, STATUS_ICON_MONITOR_PREFIX))
    {
        if (watcher->shutdown_pending)
        {
            g_debug ("A monitor appeared on the bus, cancelling shutdown\n");

            watcher->shutdown_pending = FALSE;
            g_application_hold (G_APPLICATION (watcher));

            if (watcher->owner_id == 0)
            {
                continue_startup (watcher);
                return;
            }
            else
            {
                sn_watcher_interface_set_is_status_notifier_host_registered (watcher->skeleton,
                                                                             watcher->advertise_host);
                g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (watcher->skeleton));
                sn_watcher_interface_emit_status_notifier_host_registered (watcher->skeleton);
            }
        }
    }
}

static void
handle_sn_item_name_owner_lost (XAppSnWatcher *watcher,
                                const gchar   *name,
                                const gchar   *old_owner)
{
    GList *keys, *l;

    keys = g_hash_table_get_keys (watcher->items);

    for (l = keys; l != NULL; l = l->next)
    {
        const gchar *key = l->data;

        if (g_str_has_prefix (key, name))
        {
            g_debug ("Client %s has exited, removing status icon", key);
            g_hash_table_remove (watcher->items, key);

            update_published_items (watcher);
            break;
        }
    }

    g_list_free (keys);
}

static void
handle_status_applet_name_owner_lost (XAppSnWatcher *watcher,
                                      const gchar   *name,
                                      const gchar   *old_owner)
{
    if (g_str_has_prefix (name, STATUS_ICON_MONITOR_PREFIX))
    {
        g_debug ("Lost a monitor, checking for any more");

        if (xapp_status_icon_any_monitors ())
        {
            g_debug ("Still have a monitor, continuing");

            return;
        }
        else
        {
            g_debug ("Lost our last monitor, starting countdown\n");

            if (!watcher->shutdown_pending)
            {
                watcher->shutdown_pending = TRUE;
                g_application_release (G_APPLICATION (watcher));

                sn_watcher_interface_set_is_status_notifier_host_registered (watcher->skeleton,
                                                                             FALSE);
                g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (watcher->skeleton));
            }
        }
    }
    else
    {
        handle_sn_item_name_owner_lost (watcher, name, old_owner);
    }
}

static void
name_owner_changed_signal (GDBusConnection *connection,
                           const gchar     *sender_name,
                           const gchar     *object_path,
                           const gchar     *interface_name,
                           const gchar     *signal_name,
                           GVariant        *parameters,
                           gpointer         user_data)
{
    XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);

    const gchar *name, *old_owner, *new_owner;

    g_variant_get (parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

    g_debug("XAppSnWatcher: NameOwnerChanged signal received (n: %s, old: %s, new: %s", name, old_owner, new_owner);

    if (!name)
    {
        return;
    }

    if (g_strcmp0 (new_owner, "") == 0)
    {
        handle_status_applet_name_owner_lost (watcher, name, old_owner);
    }
    else
    {
        handle_status_applet_name_owner_appeared (watcher, name, new_owner);
    }
}

static void
add_name_listener (XAppSnWatcher *watcher)
{
    g_debug ("XAppSnWatcher: Adding NameOwnerChanged listener for status monitor existence");

    watcher->name_listener_id = g_dbus_connection_signal_subscribe (watcher->connection,
                                                                    FDO_DBUS_NAME,
                                                                    FDO_DBUS_NAME,
                                                                    "NameOwnerChanged",
                                                                    FDO_DBUS_PATH,
                                                                    NULL,
                                                                    G_DBUS_SIGNAL_FLAGS_NONE,
                                                                    name_owner_changed_signal,
                                                                    watcher,
                                                                    NULL);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
    XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);

    g_debug ("Lost StatusNotifierWatcher name (maybe something replaced us), exiting immediately");
    g_application_quit (G_APPLICATION (watcher));
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);

    g_debug ("Name acquired on dbus");
    sn_watcher_interface_set_protocol_version (watcher->skeleton, 0);
    sn_watcher_interface_set_is_status_notifier_host_registered (watcher->skeleton,
                                                                 watcher->advertise_host);
    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (watcher->skeleton));
    sn_watcher_interface_emit_status_notifier_host_registered (watcher->skeleton);
}

static gboolean
handle_register_host (SnWatcherInterface     *skeleton,
                      GDBusMethodInvocation  *invocation,
                      const gchar*            service,
                      XAppSnWatcher          *watcher)
{
    // Nothing to do - we wouldn't be here if there wasn't a host (status applet)
    sn_watcher_interface_complete_register_status_notifier_host (skeleton,
                                                                 invocation);

    return TRUE;
}

static void
populate_published_list (const gchar *key,
                         gpointer     item,
                         GPtrArray   *array)
{
    g_ptr_array_add (array, g_strdup (key));
}

static void
update_published_items (XAppSnWatcher *watcher)
{
    GPtrArray *array;
    gpointer as;

    array = g_ptr_array_new ();

    g_hash_table_foreach (watcher->items, (GHFunc) populate_published_list, array);
    g_ptr_array_add (array, NULL);

    as = g_ptr_array_free (array, FALSE);

    sn_watcher_interface_set_registered_status_notifier_items (watcher->skeleton,
                                                               (const gchar * const *) as);

    g_strfreev ((gchar **) as);

    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (watcher->skeleton));
}

static gboolean
create_key (const gchar  *sender,
            const gchar  *service,
            gchar       **key,
            gchar       **bus_name,
            gchar       **path)
{
    gchar *temp_key, *temp_bname, *temp_path;

    temp_key = temp_bname = temp_path = NULL;
    *key = *bus_name = *path = NULL;

    if (g_str_has_prefix (service, "/"))
    {
        temp_bname = g_strdup (sender);
        temp_path = g_strdup (service);
    }
    else
    {
        temp_bname = g_strdup (service);
        temp_path = g_strdup ("/StatusNotifierItem");
    }

    if (!g_dbus_is_name (temp_bname))
    {
        g_free (temp_bname);
        g_free (temp_path);

        return FALSE;
    }

    temp_key = g_strdup_printf ("%s%s", temp_bname, temp_path);

    g_debug ("Key: '%s', busname '%s', path '%s'", temp_key, temp_bname, temp_path);

    *key = temp_key;
    *bus_name = temp_bname;
    *path = temp_path;

    return TRUE;
}

typedef struct
{
    XAppSnWatcher *watcher;
    GDBusMethodInvocation *invocation;
    gchar *key;
    gchar *path;
    gchar *bus_name;
    gchar *service;
} NewSnProxyData;

static void
sn_item_proxy_new_completed (GObject      *source,
                             GAsyncResult *res,
                             gpointer      user_data)
{
    NewSnProxyData *data = (NewSnProxyData *) user_data;
    XAppSnWatcher *watcher = data->watcher;
    SnItem *item;
    GError *error = NULL;

    SnItemInterface *proxy;

    proxy = sn_item_interface_proxy_new_finish (res, &error);

    if (error != NULL)
    {
        g_debug ("Could not create new status notifier proxy item for item at %s: %s",
                 data->bus_name, error->message);
        return;
    }

    item = sn_item_new ((GDBusProxy *) proxy,
                        g_str_has_prefix (data->path, APPINDICATOR_PATH_PREFIX));

    g_hash_table_insert (watcher->items,
                         g_strdup (data->key),
                         item);

    update_published_items (watcher);

    sn_watcher_interface_emit_status_notifier_item_registered (watcher->skeleton,
                                                               data->service);

    sn_watcher_interface_complete_register_status_notifier_item (watcher->skeleton,
                                                                 data->invocation);

    g_free (data->key);
    g_free (data->path);
    g_free (data->bus_name);
    g_free (data->service);
    g_object_unref (data->invocation);
    g_slice_free (NewSnProxyData, data);
}

static gboolean
handle_register_item (SnWatcherInterface     *skeleton,
                      GDBusMethodInvocation  *invocation,
                      const gchar*            service,
                      XAppSnWatcher          *watcher)
{
    SnItem *item;
    GError *error;
    const gchar *sender;
    g_autofree gchar *key = NULL, *bus_name = NULL, *path = NULL;

    sender = g_dbus_method_invocation_get_sender (invocation);

    if (!create_key (sender, service, &key, &bus_name, &path))
    {
        error = g_error_new (g_dbus_error_quark (),
                             G_DBUS_ERROR_INVALID_ARGS,
                             "Invalid bus name from: %s, %s", service, sender);
        g_dbus_method_invocation_return_gerror (invocation, error);

        return FALSE;
    }

    item = g_hash_table_lookup (watcher->items, key);

    if (item == NULL)
    {
        NewSnProxyData *data;
        error = NULL;
        g_debug ("Key: '%s'", key);

        data = g_slice_new0 (NewSnProxyData);
        data->watcher = watcher;
        data->key = g_strdup (key);
        data->path = g_strdup (path);
        data->bus_name = g_strdup (bus_name);
        data->service = g_strdup (service);
        data->invocation = g_object_ref (invocation);

        sn_item_interface_proxy_new (watcher->connection,
                                     G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                     bus_name,
                                     path,
                                     NULL,
                                     sn_item_proxy_new_completed,
                                     data);
    }

    return TRUE;
}

static gboolean
export_watcher_interface (XAppSnWatcher *watcher)
{
    GError *error = NULL;

    if (watcher->skeleton) {
        return TRUE;
    }

    watcher->skeleton = sn_watcher_interface_skeleton_new ();

    g_debug ("XAppSnWatcher: exporting StatusNotifierWatcher dbus interface to %s", NOTIFICATION_WATCHER_PATH);

    g_signal_connect (watcher->skeleton,
                      "handle-register-status-notifier-item",
                      G_CALLBACK (handle_register_item),
                      watcher);

    g_signal_connect (watcher->skeleton,
                      "handle-register-status-notifier-host",
                      G_CALLBACK (handle_register_host),
                      watcher);
    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (watcher->skeleton),
                                      watcher->connection,
                                      NOTIFICATION_WATCHER_PATH,
                                      &error);

    if (error != NULL) {
        g_critical ("XAppSnWatcher: could not export StatusNotifierWatcher interface: %s", error->message);
        g_error_free (error);

        return FALSE;
    }


    return TRUE;
}

static gboolean
on_interrupt (XAppSnWatcher *watcher)
{
    g_debug ("SIGINT - shutting down immediately");

    g_application_quit (G_APPLICATION (watcher));
    return FALSE;
}

static void
update_item_menus (const gchar *key,
                   gpointer     item,
                   gpointer     user_data)
{
    sn_item_update_menus (SN_ITEM (item));
}

static void
whitelist_changed (XAppSnWatcher *watcher)
{
    g_hash_table_foreach (watcher->items, (GHFunc) update_item_menus, NULL);
}

static void
continue_startup (XAppSnWatcher *watcher)
{
    g_debug ("Trying to acquire session bus connection");

    g_unix_signal_add (SIGINT, (GSourceFunc) on_interrupt, watcher);
    g_application_hold (G_APPLICATION (watcher));

    export_watcher_interface (watcher);

    watcher->owner_id = g_bus_own_name_on_connection (watcher->connection,
                                                      NOTIFICATION_WATCHER_NAME,
                                                      G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                                      on_name_acquired,
                                                      on_name_lost,
                                                      watcher,
                                                      NULL);
}

static void
watcher_startup (GApplication *application)
{
    XAppSnWatcher *watcher = (XAppSnWatcher*) application;
    GError *error;

    G_APPLICATION_CLASS (xapp_sn_watcher_parent_class)->startup (application);

    xapp_settings = g_settings_new (STATUS_ICON_SCHEMA);
    g_signal_connect_swapped (xapp_settings,
                              "changed::" WHITELIST_KEY,
                              G_CALLBACK (whitelist_changed),
                              watcher);

    watcher->items = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_object_unref);

    /* This buys us 30 seconds (gapp timeout) - we'll either be re-held immediately
     * because there's a monitor or exit after the 30 seconds. */
    g_application_hold (application);
    g_application_release (application);

    error = NULL;

    watcher->connection = g_bus_get_sync (G_BUS_TYPE_SESSION,
                                          NULL,
                                          &error);

    if (error != NULL)
    {
        g_critical ("Could not get session bus: %s\n", error->message);
        g_application_quit (application);
    }

    watcher->advertise_host = g_settings_get_boolean (xapp_settings, ADVERTISE_SNH_KEY);

    add_name_listener (watcher);

    if (xapp_status_icon_any_monitors ())
    {
        continue_startup (watcher);
    }
    else
    {
        g_debug ("No active monitors, exiting in 30s");
        watcher->shutdown_pending = TRUE;
    }
}

static void
watcher_finalize (GObject *object)
{
  G_OBJECT_CLASS (xapp_sn_watcher_parent_class)->finalize (object);
}

static void
watcher_shutdown (GApplication *application)
{
    XAppSnWatcher *watcher = (XAppSnWatcher *) application;

    g_clear_object (&xapp_settings);

    if (watcher->name_listener_id > 0)
    {
        g_dbus_connection_signal_unsubscribe (watcher->connection, watcher->name_listener_id);
        watcher->name_listener_id = 0;
    }

    update_published_items (watcher);
    g_clear_pointer (&watcher->items, g_hash_table_unref);

    sn_watcher_interface_set_is_status_notifier_host_registered (watcher->skeleton,
                                                                 FALSE);
    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (watcher->skeleton));
    sn_watcher_interface_emit_status_notifier_host_registered (watcher->skeleton);

    if (watcher->owner_id > 0)
    {
        g_bus_unown_name (watcher->owner_id);
    }

    if (watcher->skeleton != NULL)
    {
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (watcher->skeleton));
        g_clear_object (&watcher->skeleton);
    }

    g_clear_object (&watcher->connection);

    G_APPLICATION_CLASS (xapp_sn_watcher_parent_class)->shutdown (application);
}

static void
watcher_activate (GApplication *application)
{
}

static void
xapp_sn_watcher_init (XAppSnWatcher *watcher)
{
}

static void
xapp_sn_watcher_class_init (XAppSnWatcherClass *class)
{
  GApplicationClass *application_class = G_APPLICATION_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  application_class->startup = watcher_startup;
  application_class->shutdown = watcher_shutdown;
  application_class->activate = watcher_activate;
  object_class->finalize = watcher_finalize;
}

XAppSnWatcher *
watcher_new (const gchar *current_desktop)
{
    XAppSnWatcher *watcher;
    gboolean _register;

    g_set_application_name ("xapp-sn-watcher");

    // FIXME: xfce-session crashes if we try to register.
    _register = g_strcmp0 (current_desktop, "XFCE") != 0;

    watcher = g_object_new (xapp_sn_watcher_get_type (),
                            "application-id", "org.x.StatusNotifierWatcher",
                            "inactivity-timeout", 30000,
                            "register-session", _register,
                            NULL);

  return watcher;
}

int
main (int argc, char **argv)
{
    XAppSnWatcher *watcher;
    gchar **whitelist;
    const gchar *current_desktop;
    gboolean should_start;
    int status;

    sleep (1);

    current_desktop = g_getenv ("XDG_CURRENT_DESKTOP");
    xapp_settings = g_settings_new (STATUS_ICON_SCHEMA);

    if (g_settings_get_boolean (xapp_settings, DEBUG_KEY))
    {
        g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
    }

    whitelist = g_settings_get_strv (xapp_settings,
                                     VALID_XDG_DESKTOPS_KEY);

    should_start = g_strv_contains ((const gchar * const *) whitelist, current_desktop);

    g_strfreev (whitelist);
    g_clear_object (&xapp_settings);

    if (!should_start)
    {
        g_debug ("XDG_CURRENT_DESKTOP is '%s' - not starting XApp's StatusNotifierWatcher service."
                 "If you want to change this, add your desktop's name to the dconf org.x.apps.statusicon "
                 "'status-notifier-enabled-desktops' setting key.", current_desktop);
        exit(0);
    }

    watcher = watcher_new (current_desktop);

    status = g_application_run (G_APPLICATION (watcher), argc, argv);

    g_object_unref (watcher);

    return status;
}
