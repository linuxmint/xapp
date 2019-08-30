
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

static const gchar * DBUS_PATH = "/org/x/StatusIcon";
static const gchar * DBUS_NAME = "org.x.StatusIcon";

static const gchar introspection_xml[] =
    "<node>"
    "    <interface name='org.x.StatusIcon'>"
    "    <method name='LeftClick'>"
    "        <arg name='x' direction='in' type='i'/>"
    "        <arg name='y' direction='in' type='i'/>"
    "    </method>"
    "    <method name='MiddleClick'>"
    "        <arg name='x' direction='in' type='i'/>"
    "        <arg name='y' direction='in' type='i'/>"
    "    </method>"
    "    <method name='RightClick'>"
    "        <arg name='x' direction='in' type='i'/>"
    "        <arg name='y' direction='in' type='i'/>"
    "    </method>"
    "    <property type='s' name='Name' access='read'/>"
    "    <property type='s' name='IconName' access='read'/>"
    "    <property type='s' name='TooltipText' access='read'/>"
    "    <property type='s' name='Label' access='read'/>"
    "    <property type='b' name='Visible' access='read'/>"
    "    </interface>"
    "</node>";

enum
{
    LEFT_CLICK,
    MIDDLE_CLICK,
    RIGHT_CLICK,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

/**
 * SECTION:xapp-status-icon
 * @Short_description: Broadcasts status information over DBUS
 * @Title: XAppStatusIcon
 *
 * The XAppStatusIcon allows applications to share status info
 * about themselves.
 */

struct _XAppStatusIconPrivate
{
    gchar * name;
    gchar * icon_name;
    gchar * tooltip_text;
    gchar * label;
    gboolean visible;
    GDBusConnection* connection;
    guint owner_id;
    guint registration_id;
};

G_DEFINE_TYPE (XAppStatusIcon, xapp_status_icon, G_TYPE_OBJECT);

void
emit_changed_properties_signal (XAppStatusIcon *self)
{
    GVariantBuilder *builder;
    GError *local_error;

    if (self->priv->connection) {

        local_error = NULL;
        builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
        if (self->priv->name)
            g_variant_builder_add (builder, "{sv}", "Name", g_variant_new_string (self->priv->name));
        if (self->priv->icon_name)
            g_variant_builder_add (builder, "{sv}", "IconName", g_variant_new_string (self->priv->icon_name));
        if (self->priv->tooltip_text)
            g_variant_builder_add (builder, "{sv}", "TooltipText", g_variant_new_string (self->priv->tooltip_text));
        if (self->priv->label)
            g_variant_builder_add (builder, "{sv}", "Label", g_variant_new_string (self->priv->label));
        if (self->priv->visible)
            g_variant_builder_add (builder, "{sv}", "Visible", g_variant_new_boolean (self->priv->visible));
        g_dbus_connection_emit_signal (self->priv->connection,
                                     NULL,
                                     DBUS_PATH,
                                     "org.freedesktop.DBus.Properties",
                                     "PropertiesChanged",
                                     g_variant_new ("(sa{sv}as)",
                                                    DBUS_NAME,
                                                    builder,
                                                    NULL),
                                     &local_error);
    }
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
    XAppStatusIcon *icon = user_data;

    if (g_strcmp0 (method_name, "LeftClick") == 0)
    {
        int x, y;
        g_variant_get (parameters, "(ii)", &x, &y);
        g_dbus_method_invocation_return_value (invocation, NULL);
        g_signal_emit (icon, signals[LEFT_CLICK], 0, x, y);
    }
    else if (g_strcmp0 (method_name, "MiddleClick") == 0)
    {
        int x, y;
        g_variant_get (parameters, "(ii)", &x, &y);
        g_dbus_method_invocation_return_value (invocation, NULL);
        g_signal_emit (icon, signals[MIDDLE_CLICK], 0, x, y);
    }
    else if (g_strcmp0 (method_name, "RightClick") == 0)
    {
        int x, y;
        g_variant_get (parameters, "(ii)", &x, &y);
        g_dbus_method_invocation_return_value (invocation, NULL);
        g_signal_emit (icon, signals[RIGHT_CLICK], 0, x, y);
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
    XAppStatusIcon *icon = user_data;
    ret = NULL;

    if (icon->priv->name && g_strcmp0 (property_name, "Name") == 0)
    {
        ret = g_variant_new_string (icon->priv->name);
    }
    else if (icon->priv->icon_name && g_strcmp0 (property_name, "IconName") == 0)
    {
        ret = g_variant_new_string (icon->priv->icon_name);
    }
    else if (icon->priv->tooltip_text && g_strcmp0 (property_name, "TooltipText") == 0)
    {
        ret = g_variant_new_string (icon->priv->tooltip_text);
    }
    else if (icon->priv->label && g_strcmp0 (property_name, "Label") == 0)
    {
        ret = g_variant_new_string (icon->priv->label);
    }
    else if (g_strcmp0 (property_name, "Visible") == 0)
    {
        ret = g_variant_new_boolean (icon->priv->visible);
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
    XAppStatusIcon *icon = user_data;

    icon->priv->connection = connection;

    GDBusNodeInfo *introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
    icon->priv->registration_id = g_dbus_connection_register_object (connection,
                     DBUS_PATH,
                     introspection_data->interfaces[0],
                     &interface_vtable,
                     icon,  /* user_data */
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
xapp_status_icon_init (XAppStatusIcon *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, XAPP_TYPE_STATUS_ICON, XAppStatusIconPrivate);
    self->priv->name = g_strdup_printf("%s", g_get_application_name());
    self->priv->icon_name = NULL;
    self->priv->tooltip_text = NULL;
    self->priv->label = NULL;
    self->priv->visible = TRUE;
    self->priv->connection = NULL;
    self->priv->owner_id = 0;
    self->priv->registration_id = 0;

    char *owner_name = g_strdup_printf("%s.PID-%d", DBUS_NAME, getpid());
    self->priv->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                owner_name,
                G_DBUS_CONNECTION_FLAGS_NONE,
                on_bus_acquired,
                on_name_acquired,
                on_name_lost,
                self,
                NULL);

    g_free(owner_name);

    signals[LEFT_CLICK] =
        g_signal_new ("left-click",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      NULL,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    signals[MIDDLE_CLICK] =
        g_signal_new ("middle-click",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      NULL,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    signals[RIGHT_CLICK] =
        g_signal_new ("right-click",
                      XAPP_TYPE_STATUS_ICON,
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      NULL,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
}

static void
xapp_status_icon_finalize (GObject *object)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (object);

    g_free (self->priv->name);
    g_free (self->priv->icon_name);
    g_free (self->priv->tooltip_text);
    g_free (self->priv->label);

    if (self->priv->connection != NULL && self->priv->registration_id > 0) {
        g_dbus_connection_unregister_object(self->priv->connection, self->priv->registration_id);
    }

    if (self->priv->owner_id > 0) {
        g_bus_unown_name(self->priv->owner_id);
    }

    G_OBJECT_CLASS (xapp_status_icon_parent_class)->finalize (object);
}

static void
xapp_status_icon_class_init (XAppStatusIconClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = xapp_status_icon_finalize;

    g_type_class_add_private (gobject_class, sizeof (XAppStatusIconPrivate));
}

/**
 * xapp_status_icon_set_name:
 * @self: a #XAppStatusIcon
 *
 * Sets the icon name
 */

void
xapp_status_icon_set_name (XAppStatusIcon *self, const gchar *name)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (self));
    g_free (self->priv->name);
    self->priv->name = g_strdup (name);
    emit_changed_properties_signal (self);
}

/**
 * xapp_status_icon_set_icon_name:
 * @self: a #XAppStatusIcon
 *
 * Sets the icon name
 */

void
xapp_status_icon_set_icon_name (XAppStatusIcon *self, const gchar *icon_name)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (self));
    g_free (self->priv->icon_name);
    self->priv->icon_name = g_strdup (icon_name);
    emit_changed_properties_signal (self);
}

/**
 * xapp_status_icon_set_tooltip_text:
 * @self: a #XAppStatusIcon
 *
 * Sets the app name
 */

void
xapp_status_icon_set_tooltip_text (XAppStatusIcon *self, const gchar *tooltip_text)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (self));
    g_free (self->priv->tooltip_text);
    self->priv->tooltip_text = g_strdup (tooltip_text);
    emit_changed_properties_signal (self);
}

/**
 * xapp_status_icon_set_label:
 * @self: a #XAppStatusIcon
 *
 * Sets the label
 */

void
xapp_status_icon_set_label (XAppStatusIcon *self, const gchar *label)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (self));
    g_free (self->priv->label);
    self->priv->label = g_strdup (label);
    emit_changed_properties_signal (self);
}

void
xapp_status_icon_set_visible (XAppStatusIcon *self, const gboolean visible)
{
    g_return_if_fail (XAPP_IS_STATUS_ICON (self));
    self->priv->visible = visible;
    emit_changed_properties_signal (self);
}