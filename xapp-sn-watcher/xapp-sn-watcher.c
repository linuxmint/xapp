#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include <libxapp/xapp-status-icon.h>
#include <libxapp/xapp-util.h>

#define DEBUG_FLAG XAPP_DEBUG_SN_WATCHER
#include <libxapp/xapp-debug.h>

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
    GCancellable *cancellable;

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
            DEBUG ("A monitor appeared on the bus, cancelling shutdown\n");

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
            DEBUG ("Client %s has exited, removing status icon", key);
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
        DEBUG ("Lost a monitor, checking for any more");

        if (xapp_status_icon_any_monitors ())
        {
            DEBUG ("Still have a monitor, continuing");

            return;
        }
        else
        {
            DEBUG ("Lost our last monitor, starting countdown\n");

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

    DEBUG("NameOwnerChanged signal received (n: %s, old: %s, new: %s", name, old_owner, new_owner);

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
    DEBUG ("Adding NameOwnerChanged listener for status monitor existence");

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

    DEBUG ("Lost StatusNotifierWatcher name (maybe something replaced us), exiting immediately");
    g_application_quit (G_APPLICATION (watcher));
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);

    DEBUG ("Name acquired on dbus");
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

    DEBUG ("Key: '%s', busname '%s', path '%s'", temp_key, temp_bname, temp_path);

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
free_sn_proxy_data (NewSnProxyData *data)
{
    g_free (data->key);
    g_free (data->path);
    g_free (data->bus_name);
    g_free (data->service);
    g_object_unref (data->invocation);

    g_slice_free (NewSnProxyData, data);
}

static void
sn_item_proxy_new_completed (GObject      *source,
                             GAsyncResult *res,
                             gpointer      user_data)
{
    NewSnProxyData *data = (NewSnProxyData *) user_data;
    XAppSnWatcher *watcher = data->watcher;
    SnItem *item;
    gpointer stolen_ptr;
    GError *error = NULL;

    SnItemInterface *proxy;

    proxy = sn_item_interface_proxy_new_finish (res, &error);

    g_hash_table_steal_extended (watcher->items,
                                 data->key,
                                 &stolen_ptr,
                                 NULL);

    if ((gchar *) stolen_ptr == NULL)
    {
        g_dbus_method_invocation_return_error (data->invocation,
                                               G_DBUS_ERROR,
                                               G_DBUS_ERROR_FAILED,
                                               "New StatusNotifierItem disappeared before "
                                               "its registration was complete.");

        free_sn_proxy_data (data);
        g_clear_object (&proxy);
        return;
    }

    if (error != NULL)
    {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            DEBUG ("Could not create new status notifier proxy item for item at %s: %s",
                     data->bus_name, error->message);
        }

        g_free (stolen_ptr);
        free_sn_proxy_data (data);

        g_dbus_method_invocation_take_error (data->invocation, error);
        return;
    }

    item = sn_item_new ((GDBusProxy *) proxy,
                        g_str_has_prefix (data->path, APPINDICATOR_PATH_PREFIX));

    g_hash_table_insert (watcher->items,
                         stolen_ptr,
                         item);

    update_published_items (watcher);

    sn_watcher_interface_emit_status_notifier_item_registered (watcher->skeleton,
                                                               data->service);

    sn_watcher_interface_complete_register_status_notifier_item (watcher->skeleton,
                                                                 data->invocation);

    free_sn_proxy_data (data);
}

static gboolean
handle_register_item (SnWatcherInterface     *skeleton,
                      GDBusMethodInvocation  *invocation,
                      const gchar*            service,
                      XAppSnWatcher          *watcher)
{
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

    if (!g_hash_table_contains (watcher->items, (const gchar *) key))
    {
        NewSnProxyData *data;
        error = NULL;
        // DEBUG ("Key: '%s'", key);

        data = g_slice_new0 (NewSnProxyData);
        data->watcher = watcher;
        data->key = g_strdup (key);
        data->path = g_strdup (path);
        data->bus_name = g_strdup (bus_name);
        data->service = g_strdup (service);
        data->invocation = g_object_ref (invocation);

        /* Save a place in the hash table, some programs re-register frequently,
         * and we don't want the same app have two registrations here. */
        g_hash_table_insert (watcher->items,
                             g_strdup (key),
                             NULL);

        sn_item_interface_proxy_new (watcher->connection,
                                     G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                     bus_name,
                                     path,
                                     watcher->cancellable,
                                     sn_item_proxy_new_completed,
                                     data);
    }
    else
    {
        sn_watcher_interface_complete_register_status_notifier_item (watcher->skeleton,
                                                                     invocation);
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

    DEBUG ("Exporting StatusNotifierWatcher dbus interface to %s", NOTIFICATION_WATCHER_PATH);

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
    DEBUG ("SIGINT - shutting down immediately");

    g_application_quit (G_APPLICATION (watcher));
    return FALSE;
}

static void
continue_startup (XAppSnWatcher *watcher)
{
    DEBUG ("Trying to acquire session bus connection");

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
unref_proxy (gpointer data)
{
    // if g_hash_table_remove is called from handle_sn_item_name_owner_lost
    // *before* sn_item_proxy_new_completed is complete, the key will be
    // pointing to a NULL value, so avoid trying to free it.

    if (data == NULL)
    {
        return;
    }

    SnItem *item = SN_ITEM (data);

    if (item)
    {
        g_object_unref (item);
    }
}

static void
clean_tempfiles (XAppSnWatcher *watcher)
{
    gchar *cmd = g_strdup_printf ("rm -f %s/xapp-tmp-*.png", xapp_get_tmp_dir ());

    // -Wunused-result
    if (system(cmd)) ;

    g_free (cmd);
}

static void
watcher_startup (GApplication *application)
{
    XAppSnWatcher *watcher = (XAppSnWatcher*) application;
    GError *error;

    G_APPLICATION_CLASS (xapp_sn_watcher_parent_class)->startup (application);

    xapp_settings = g_settings_new (STATUS_ICON_SCHEMA);

    watcher->items = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, (GDestroyNotify) unref_proxy);

    /* This buys us 30 seconds (gapp timeout) - we'll either be re-held immediately
     * because there's a monitor or exit after the 30 seconds. */
    g_application_hold (application);
    g_application_release (application);

    watcher->cancellable = g_cancellable_new ();

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
        DEBUG ("No active monitors, exiting in 30s");
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
    g_clear_object (&watcher->cancellable);

    clean_tempfiles (watcher);

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

static void
muted_log_handler (const char     *log_domain,
                   GLogLevelFlags  log_level,
                   const char     *message,
                   gpointer        data)

{
}

int
main (int argc, char **argv)
{
    XAppSnWatcher *watcher;
    gchar **whitelist;
    const gchar *current_desktop;
    gboolean should_start;
    int status;

    if (xapp_util_get_session_is_running ())
    {
        // The session manager tries to restart this immediately in the event of a crash,
        // and we need a small delay between instances of xapp-sn-watcher to allow dbus and
        // status-notifier apps to cleanly process removal of the old one.
        //
        // Skip this at startup, as it would cause a delay during login, and there's no
        // existing process to replace anyhow.
        sleep (2);
    }

    xapp_settings = g_settings_new (STATUS_ICON_SCHEMA);

    if (g_settings_get_boolean (xapp_settings, DEBUG_KEY))
    {
        g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

        gchar *flags = g_settings_get_string (xapp_settings, DEBUG_FLAGS_KEY);
        g_setenv ("XAPP_DEBUG", flags, TRUE);
        g_free (flags);

    }

    whitelist = g_settings_get_strv (xapp_settings,
                                     VALID_XDG_DESKTOPS_KEY);

    current_desktop = g_getenv ("XDG_CURRENT_DESKTOP");

    if (current_desktop != NULL)
    {
        should_start = g_strv_contains ((const gchar * const *) whitelist, current_desktop);
    }
    else
    {
        g_warning ("XDG_CURRENT_DESKTOP not set, unable to check against enabled desktop list. Starting anyway...");
        should_start = TRUE;
    }

    g_strfreev (whitelist);
    g_clear_object (&xapp_settings);

    if (!should_start)
    {
        DEBUG ("XDG_CURRENT_DESKTOP is '%s' - not starting XApp's StatusNotifierWatcher service."
                 "If you want to change this, add your desktop's name to the dconf org.x.apps.statusicon "
                 "'status-notifier-enabled-desktops' setting key.", current_desktop);
        exit(0);
    }

    // libdbusmenu and gtk throw a lot of menu-related warnings for problems we already handle.
    // They can be noisy in .xsession-errors, however. Redirect them to debug output only.
    DEBUG (""); // Initialize the DEBUGGING flag
    if (!DEBUGGING)
    {
        g_log_set_handler ("LIBDBUSMENU-GTK", G_LOG_LEVEL_WARNING,
                           muted_log_handler, NULL);
        g_log_set_handler ("LIBDBUSMENU-GLIB", G_LOG_LEVEL_WARNING,
                           muted_log_handler, NULL);
        g_log_set_handler ("Gtk", G_LOG_LEVEL_CRITICAL,
                           muted_log_handler, NULL);
    }

    watcher = watcher_new (current_desktop);

    status = g_application_run (G_APPLICATION (watcher), argc, argv);

    g_object_unref (watcher);

    return status;
}
