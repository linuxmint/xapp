/* xapp-status-icon-native.c
 *
 * Native backend for XAppStatusIcon (org.x.StatusIcon interface)
 * Uses XAppStatusIconMonitor (Mint/Cinnamon/MATE/Xfce panel applets)
 */

#include <config.h>
#include <gtk/gtk.h>

#include "xapp-status-icon.h"
#include "xapp-status-icon-private.h"
#include "xapp-status-icon-backend.h"
#include "xapp-statusicon-interface.h"

#define DEBUG_FLAG XAPP_DEBUG_STATUS_ICON
#include "xapp-debug.h"

/* Constants */
#define FDO_DBUS_NAME "org.freedesktop.DBus"
#define FDO_DBUS_PATH "/org/freedesktop/DBus"

#define ICON_BASE_PATH "/org/x/StatusIcon"
#define ICON_SUB_PATH (ICON_BASE_PATH "/Icon")
#define ICON_NAME "org.x.StatusIcon"

#define STATUS_ICON_MONITOR_MATCH "org.x.StatusIconMonitor"

#define MAX_NAME_FAILS 3

/* Global variables from main file */
extern guint status_icon_signals[];
extern XAppStatusIconState process_icon_state;

/* Native backend D-Bus object manager infrastructure */
static GDBusObjectManagerServer *obj_server = NULL;
static guint name_owner_id = 0;

/* Forward declarations */
static void obj_server_finalized (gpointer data, GObject *object);
static void ensure_object_manager (XAppStatusIcon *self);

static gboolean handle_click_method (XAppStatusIconInterface *skeleton,
                                    GDBusMethodInvocation   *invocation,
                                    gint                     x,
                                    gint                     y,
                                    guint                    button,
                                    guint                    _time,
                                    gint                     panel_position,
                                    XAppStatusIcon          *icon);

static gboolean handle_scroll_method (XAppStatusIconInterface *skeleton,
                                     GDBusMethodInvocation   *invocation,
                                     gint                     delta,
                                     XAppScrollDirection      direction,
                                     guint                    _time,
                                     XAppStatusIcon          *icon);

/* Structure for signal connections */
typedef struct
{
    const gchar  *signal_name;
    gpointer      callback;
} SkeletonSignal;

static SkeletonSignal skeleton_status_icon_signals[] = {
    { "handle-button-press",     handle_click_method },
    { "handle-button-release",   handle_click_method },
    { "handle-scroll",           handle_scroll_method }
};

/* Native backend object manager infrastructure */

static void
obj_server_finalized (gpointer  data,
                      GObject  *object)
{
    DEBUG ("Final icon removed, clearing object manager (%s)", g_get_prgname ());

    if (name_owner_id > 0)
    {
        g_bus_unown_name (name_owner_id);
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
        g_dbus_object_manager_server_set_connection (obj_server, XAPP_STATUS_ICON_GET_PRIVATE(self)->connection);
        g_object_weak_ref (G_OBJECT (obj_server), (GWeakNotify) obj_server_finalized, self);
    }
    else
    {
        g_object_ref (obj_server);
    }
}

/* Native backend D-Bus method handlers */

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
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);
    const gchar *name = g_dbus_method_invocation_get_method_name (invocation);

    if (g_strcmp0 (name, "ButtonPress") == 0)
    {
        DEBUG ("Received ButtonPress from monitor %s: "
                 "pos:%d,%d , button: %s , time: %u , orientation: %s",
                 g_dbus_method_invocation_get_sender (invocation),
                 x, y, button_to_str (button), _time, panel_position_to_str (panel_position));

        if (should_send_activate (button, priv->have_button_press))
        {
            DEBUG ("Native sending 'activate' for %s button", button_to_str (button));
            emit_activate (icon, button, _time);
        }

        priv->have_button_press = TRUE;

        emit_button_press (icon, x, y, button, _time, panel_position);

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

        if (priv->have_button_press)
        {
            GtkMenu *menu_to_use = get_menu_to_use (icon, button);

            if (menu_to_use)
            {
                popup_menu (icon,
                            menu_to_use,
                            x, y,
                            button,
                            _time,
                            panel_position);
            }

            emit_button_release (icon, x, y, button, _time, panel_position);
        }

        priv->have_button_press = FALSE;

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
             delta, scroll_direction_to_str (direction), _time);

    emit_scroll (icon, delta, direction, _time);

    xapp_status_icon_interface_complete_scroll (skeleton,
                                                invocation);

    return TRUE;
}

/* Native backend lifecycle and management functions */

static void
sync_skeleton (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);
    DEBUG ("Syncing icon properties (%s)", priv->name);

    priv->fail_counter = 0;

    g_clear_object (&priv->gtk_status_icon);

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
    process_icon_state = XAPP_STATUS_ICON_STATE_NATIVE;

    GList *instances = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (obj_server));
    GList *l;

    for (l = instances; l != NULL; l = l->next)
    {
        GObject *instance = G_OBJECT (l->data);
        XAppStatusIcon *icon = XAPP_STATUS_ICON (g_object_get_data (instance, "xapp-status-icon-instance"));

        if (icon == NULL)
        {
            g_warning ("on_name_aquired: Could not retrieve xapp-status-icon-instance data: %s", name);
            continue;
        }

        sync_skeleton (icon);

        DEBUG ("Name acquired on dbus, state is now: %s",
               state_to_str (process_icon_state));

        emit_state_changed (icon, process_icon_state);
    }

    g_list_free_full (instances, g_object_unref);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
    g_warning ("XAppStatusIcon: lost or could not acquire presence on dbus.  Refreshing.");

    GList *instances = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (obj_server));
    GList *l;

    for (l = instances; l != NULL; l = l->next)
    {
        GObject *instance = G_OBJECT (l->data);
        XAppStatusIcon *icon = XAPP_STATUS_ICON (g_object_get_data (instance, "xapp-status-icon-instance"));

        if (icon == NULL)
        {
            g_warning ("on_name_lost: Could not retrieve xapp-status-icon-instance data: %s", name);
            continue;
        }

        XAPP_STATUS_ICON_GET_PRIVATE(icon)->fail_counter++;
        refresh_icon (icon);
    }

    g_list_free_full (instances, g_object_unref);
}

static gboolean
export_icon_interface (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);
    gint i;

    ensure_object_manager (self);

    if (priv->interface_skeleton)
    {
        return TRUE;
    }

    priv->object_skeleton = xapp_object_skeleton_new (ICON_SUB_PATH);
    priv->interface_skeleton = xapp_status_icon_interface_skeleton_new ();

    xapp_object_skeleton_set_status_icon_interface (priv->object_skeleton,
                                                    priv->interface_skeleton);

    g_object_set_data (G_OBJECT (priv->object_skeleton), "xapp-status-icon-instance", self);

    g_dbus_object_manager_server_export_uniquely (obj_server,
                                                  G_DBUS_OBJECT_SKELETON (priv->object_skeleton));

    g_object_unref (priv->object_skeleton);
    g_object_unref (priv->interface_skeleton);

    for (i = 0; i < G_N_ELEMENTS (skeleton_status_icon_signals); i++) {
            SkeletonSignal sig = skeleton_status_icon_signals[i];

            g_signal_connect (priv->interface_skeleton,
                              sig.signal_name,
                              G_CALLBACK (sig.callback),
                              self);
    }

    return TRUE;
}

static void
connect_with_status_applet (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);
    gchar **name_parts = NULL;
    gchar *owner_name;

    name_parts = g_strsplit (priv->name, ".", -1);

    if (g_dbus_is_name (priv->name) &&
        g_str_has_prefix (priv->name, ICON_NAME) &&
        g_strv_length (name_parts) == 4)
    {
        owner_name = g_strdup (priv->name);
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
        name_owner_id = g_bus_own_name_on_connection (priv->connection,
                                                      owner_name,
                                                      G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
                                                      on_name_acquired,
                                                      on_name_lost,
                                                      NULL,
                                                      NULL);
    }

    g_free (owner_name);
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
            // Fall back to GtkStatusIcon - signal to switch backends
            refresh_icon (self);
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
        // No monitor found, signal to switch backends
        refresh_icon (self);
    }
}

static void
look_for_status_applet (XAppStatusIcon *self)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);

    DEBUG("Looking for status monitors");

    cancellable_reset (self);

    g_dbus_connection_call (priv->connection,
                            FDO_DBUS_NAME,
                            FDO_DBUS_PATH,
                            FDO_DBUS_NAME,
                            "ListNames",
                            NULL,
                            G_VARIANT_TYPE ("(as)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            3000, /* 3 secs */
                            priv->cancellable,
                            on_list_names_completed,
                            self);
}

/* Backend operations implementations */

gboolean
native_backend_init (XAppStatusIcon *icon)
{
    DEBUG("Native backend init - looking for status applet");
    look_for_status_applet (icon);
    return TRUE;
}

void
native_backend_cleanup (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("Native backend cleanup");

    if (priv->object_skeleton)
    {
        const gchar *path;
        path = g_dbus_object_get_object_path (G_DBUS_OBJECT (priv->object_skeleton));

        DEBUG ("Removing interface at path '%s'", path);

        g_object_set_data (G_OBJECT (priv->object_skeleton), "xapp-status-icon-instance", NULL);
        g_dbus_object_manager_server_unexport (obj_server, path);

        priv->interface_skeleton = NULL;
        priv->object_skeleton = NULL;

        g_object_unref (obj_server);
    }
}

void
native_backend_sync (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("Native backend sync");

    if (priv->interface_skeleton)
    {
        sync_skeleton (icon);
    }
}

void
native_backend_set_icon_name (XAppStatusIcon *icon, const gchar *icon_name)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_icon_name (priv->interface_skeleton, icon_name);
    }
}

void
native_backend_set_tooltip (XAppStatusIcon *icon, const gchar *tooltip)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_tooltip_text (priv->interface_skeleton, tooltip);
    }
}

void
native_backend_set_visible (XAppStatusIcon *icon, gboolean visible)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_visible (priv->interface_skeleton, visible);
    }
}

void
native_backend_set_label (XAppStatusIcon *icon, const gchar *label)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (priv->interface_skeleton)
    {
        xapp_status_icon_interface_set_label (priv->interface_skeleton, label);
    }
}

/* Backend operations structure */
XAppBackend native_backend_ops = {
    .type = XAPP_BACKEND_NATIVE,
    .init = native_backend_init,
    .cleanup = native_backend_cleanup,
    .sync = native_backend_sync,
    .set_icon_name = native_backend_set_icon_name,
    .set_tooltip = native_backend_set_tooltip,
    .set_visible = native_backend_set_visible,
    .set_label = native_backend_set_label,
};
