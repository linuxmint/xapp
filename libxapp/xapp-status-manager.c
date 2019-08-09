
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include "xapp-status-manager.h"

static const gchar introspection_xml[] =
    "<node>"
    "    <interface name='org.x.Status'>"
    "    <method name='Wake'/>"
    "    <property type='s' name='AppName' access='read'/>"
    "    <property type='s' name='Label' access='read'/>"
    "    <property type='s' name='IconName' access='read'/>"
    "    </interface>"
    "</node>";

/**
 * SECTION:xapp-status-manager
 * @Short_description: Broadcasts status information over DBUS
 * @Title: XAppStatusManager
 *
 * The XAppStatusManager allows applications to share status info
 * about themselves.
 */

struct _XAppStatusManagerPrivate
{
    gchar * app_name;
    gchar * label;
    gchar * icon_name;
    GDBusConnection* connection;
    guint owner_id;
    guint registration_id;
};

G_DEFINE_TYPE (XAppStatusManager, xapp_status_manager, G_TYPE_OBJECT);

void
emit_changed_properties_signal (XAppStatusManager *self)
{
    GVariantBuilder *builder;
    GError *local_error;

    local_error = NULL;
    builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
    g_variant_builder_add (builder, "{sv}", "AppName", g_variant_new_string (self->priv->app_name));
    g_variant_builder_add (builder, "{sv}", "Label", g_variant_new_string (self->priv->label));
    g_variant_builder_add (builder, "{sv}", "IconName", g_variant_new_string (self->priv->icon_name));
    g_dbus_connection_emit_signal (self->priv->connection,
                                 NULL,
                                 "/org/x/Status",
                                 "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged",
                                 g_variant_new ("(sa{sv}as)",
                                                "org.x.Status",
                                                builder,
                                                NULL),
                                 &local_error);
}

void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
  if (g_strcmp0 (method_name, "Wake") == 0)
    {
      g_dbus_method_invocation_return_value (invocation, NULL);
      // TODO: Emit a signal
    }
}

GVariant *
get_property (GDBusConnection  *connection,
             const gchar      *sender,
             const gchar      *object_path,
             const gchar      *interface_name,
             const gchar      *property_name,
             GError          **error,
             gpointer          user_data)
{
    GVariant *ret;
    XAppStatusManager *manager = user_data;
    ret = NULL;

    if (g_strcmp0 (property_name, "AppName") == 0)
    {
        ret = g_variant_new_string (manager->priv->app_name);
    }
    else if (g_strcmp0 (property_name, "Label") == 0)
    {
        ret = g_variant_new_string (manager->priv->label);
    }
    else if (g_strcmp0 (property_name, "IconName") == 0)
    {
        ret = g_variant_new_string (manager->priv->icon_name);
    }

    return ret;
}

static const GDBusInterfaceVTable interface_vtable =
{
  handle_method_call,
  get_property,
  NULL
};

void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    XAppStatusManager *manager = user_data;

    manager->priv->connection = connection;

    GDBusNodeInfo *introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
    manager->priv->registration_id = g_dbus_connection_register_object (connection,
                     "/org/x/Status",
                     introspection_data->interfaces[0],
                     &interface_vtable,
                     manager,  /* user_data */
                     NULL,  /* user_data_free_func */
                     NULL); /* GError** */
}

void on_name_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
}

void on_name_lost (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
}

static void
xapp_status_manager_init (XAppStatusManager *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, XAPP_TYPE_STATUS_MANAGER, XAppStatusManagerPrivate);
    self->priv->app_name = g_strdup("");
    self->priv->label = g_strdup("");
    self->priv->icon_name = g_strdup("");
    self->priv->connection = NULL;
    self->priv->owner_id = 0;
    self->priv->registration_id = 0;

    char *owner_name = g_strdup_printf("org.x.Status.PID-%d", getpid());
    self->priv->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
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
xapp_status_manager_finalize (GObject *object)
{
    XAppStatusManager *self = XAPP_STATUS_MANAGER (object);

    g_free (self->priv->app_name);
    g_free (self->priv->label);
    g_free (self->priv->icon_name);

    if (self->priv->connection != NULL && self->priv->registration_id > 0) {
        g_dbus_connection_unregister_object(self->priv->connection, self->priv->registration_id);
    }

    if (self->priv->owner_id > 0) {
        g_bus_unown_name(self->priv->owner_id);
    }

    G_OBJECT_CLASS (xapp_status_manager_parent_class)->finalize (object);
}

static void
xapp_status_manager_class_init (XAppStatusManagerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = xapp_status_manager_finalize;

    g_type_class_add_private (gobject_class, sizeof (XAppStatusManagerPrivate));
}

/**
 * xapp_status_manager_set_app_name:
 * @self: a #XAppStatusManager
 *
 * Sets the app name
 */

void
xapp_status_manager_set_app_name (XAppStatusManager *self, const gchar *app_name)
{
    g_return_if_fail (XAPP_IS_STATUS_MANAGER (self));
    g_free (self->priv->app_name);
    self->priv->app_name = g_strdup (app_name);
    emit_changed_properties_signal (self);
}

/**
 * xapp_status_manager_set_label:
 * @self: a #XAppStatusManager
 *
 * Sets the label
 */

void
xapp_status_manager_set_label (XAppStatusManager *self, const gchar *label)
{
    g_return_if_fail (XAPP_IS_STATUS_MANAGER (self));
    g_free (self->priv->label);
    self->priv->label = g_strdup (label);
    emit_changed_properties_signal (self);
}

/**
 * xapp_status_manager_set_icon_name:
 * @self: a #XAppStatusManager
 *
 * Sets the icon name
 */

void
xapp_status_manager_set_icon_name (XAppStatusManager *self, const gchar *icon_name)
{
    g_return_if_fail (XAPP_IS_STATUS_MANAGER (self));
    g_free (self->priv->icon_name);
    self->priv->icon_name = g_strdup (icon_name);
    emit_changed_properties_signal (self);
}

