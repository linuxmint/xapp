
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include "xapp-status-icon.h"
#include "xapp-status-icon-private.h"
#include "xapp-status-icon-backend.h"
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
// For the first icon, it gets created when exporting the icon's interface.
XAppStatusIconState process_icon_state = XAPP_STATUS_ICON_STATE_NO_SUPPORT;

/* Signal array - exported for backend files */
guint status_icon_signals[SIGNAL_LAST] = {0, };

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
G_DEFINE_TYPE_WITH_PRIVATE (XAppStatusIcon, xapp_status_icon, G_TYPE_OBJECT)

/* Wrapper function to access private data from other compilation units.
 * G_DEFINE_TYPE_WITH_PRIVATE generates an inline static version,
 * but we need a non-inline version for the backend files to link against. */
XAppStatusIconPrivate *
_xapp_status_icon_get_priv (XAppStatusIcon *self)
{
    return xapp_status_icon_get_instance_private (self);
}

void refresh_icon        (XAppStatusIcon *self);

/* Backend declarations */
extern XAppBackend native_backend_ops;
extern XAppBackend sni_backend_ops;
extern XAppBackend fallback_backend_ops;

/* Common utility functions now in xapp-status-icon-common.c */

/* Monitor for status applets appearing/disappearing on D-Bus */
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

    XAPP_STATUS_ICON_GET_PRIVATE(self)->listener_id = g_dbus_connection_signal_subscribe (XAPP_STATUS_ICON_GET_PRIVATE(self)->connection,
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

/* Backend switching function */
void
switch_to_backend (XAppStatusIcon *self, XAppBackendType backend_type)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(self);

    if (priv->active_backend == backend_type)
    {
        DEBUG ("Already using %d backend", backend_type);
        return;
    }

    DEBUG ("Switching to backend type %d", backend_type);

    /* Cleanup old backend */
    if (priv->backend_ops && priv->backend_ops->cleanup)
    {
        priv->backend_ops->cleanup(self);
    }

    /* Set new backend */
    priv->active_backend = backend_type;

    switch (backend_type)
    {
        case XAPP_BACKEND_NATIVE:
            priv->backend_ops = &native_backend_ops;
            break;
        case XAPP_BACKEND_SNI:
            priv->backend_ops = &sni_backend_ops;
            break;
        case XAPP_BACKEND_FALLBACK:
            priv->backend_ops = &fallback_backend_ops;
            break;
        default:
            priv->backend_ops = NULL;
            DEBUG ("Unknown backend type %d", backend_type);
            return;
    }

    /* Initialize new backend */
    gboolean success = FALSE;
    if (priv->backend_ops && priv->backend_ops->init)
    {
        success = priv->backend_ops->init(self);
    }

    if (success && priv->backend_ops && priv->backend_ops->sync)
    {
        /* Sync all properties to new backend */
        priv->backend_ops->sync(self);
    }
}

static XAppBackendType
get_forced_backend (void)
{
    const gchar *env = g_getenv ("XAPP_STATUS_ICON_USE_BACKEND");

    if (env == NULL)
    {
        return XAPP_BACKEND_NONE;
    }

    if (g_strcmp0 (env, "native") == 0)
    {
        DEBUG ("Forcing native backend via environment variable");
        return XAPP_BACKEND_NATIVE;
    }
    else if (g_strcmp0 (env, "sni") == 0)
    {
        DEBUG ("Forcing SNI backend via environment variable");
        return XAPP_BACKEND_SNI;
    }
    else if (g_strcmp0 (env, "xembed") == 0)
    {
        DEBUG ("Forcing xembed (fallback) backend via environment variable");
        return XAPP_BACKEND_FALLBACK;
    }

    g_warning ("Invalid XAPP_STATUS_ICON_USE_BACKEND value '%s'. Valid values: native, sni, xembed", env);
    return XAPP_BACKEND_NONE;
}

static void
complete_icon_setup (XAppStatusIcon *self)
{
    XAppBackendType forced_backend;

    if (XAPP_STATUS_ICON_GET_PRIVATE(self)->listener_id == 0)
        {
            add_name_listener (self);
        }

    /* Check if a backend is forced via environment variable */
    forced_backend = get_forced_backend ();
    if (forced_backend != XAPP_BACKEND_NONE)
    {
        switch_to_backend (self, forced_backend);
        return;
    }

    /* There is a potential loop in the g_bus_own_name sequence -
     * if we fail to acquire a name, we refresh again and potentially
     * fail again.  If we fail more than MAX_NAME_FAILS, then quit trying
     * and just use the fallback icon   It's pretty unlikely for*/
    if (XAPP_STATUS_ICON_GET_PRIVATE(self)->fail_counter == MAX_NAME_FAILS)
    {
        switch_to_backend (self, XAPP_BACKEND_FALLBACK);
        return;
    }

    /* Try native backend first */
    switch_to_backend (self, XAPP_BACKEND_NATIVE);
}

static void
on_session_bus_connected (GObject      *source,
                          GAsyncResult *res,
                          gpointer      user_data)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (user_data);
    GError *error;

    error = NULL;

    XAPP_STATUS_ICON_GET_PRIVATE(self)->connection = g_bus_get_finish (res, &error);

    if (error != NULL)
    {
        if (error->code != G_IO_ERROR_CANCELLED)
        {
            g_critical ("XAppStatusIcon: Unable to acquire session bus: %s", error->message);


            /* If we never get a connection, we use the Gtk icon exclusively, and will never
             * re-try.  FIXME? this is unlikely to happen, so I don't see the point in trying
             * later, as there are probably bigger problems in this case. */
            switch_to_backend (self, XAPP_BACKEND_FALLBACK);
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

void
refresh_icon (XAppStatusIcon *self)
{
    if (XAPP_STATUS_ICON_GET_PRIVATE(self)->connection == NULL)
    {
        DEBUG ("Connecting to session bus");

        cancellable_reset (self);

        g_bus_get (G_BUS_TYPE_SESSION,
                   XAPP_STATUS_ICON_GET_PRIVATE(self)->cancellable,
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
            XAPP_STATUS_ICON_GET_PRIVATE(XAPP_STATUS_ICON (object))->icon_size = CLAMP (g_value_get_int (value), 0, MAX_SANE_ICON_SIZE);
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
            g_value_set_object (value, XAPP_STATUS_ICON_GET_PRIVATE(icon)->primary_menu);
            break;
        case PROP_SECONDARY_MENU:
            g_value_set_object (value, XAPP_STATUS_ICON_GET_PRIVATE(icon)->secondary_menu);
            break;
        case PROP_ICON_SIZE:
            g_value_set_int (value, XAPP_STATUS_ICON_GET_PRIVATE(icon)->icon_size);
            break;
        case PROP_NAME:
            g_value_set_string (value, XAPP_STATUS_ICON_GET_PRIVATE(icon)->name);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_status_icon_init (XAppStatusIcon *self)
{
    XAPP_STATUS_ICON_GET_PRIVATE(self)->name = g_strdup (g_get_prgname());

    XAPP_STATUS_ICON_GET_PRIVATE(self)->icon_size = FALLBACK_ICON_SIZE;
    XAPP_STATUS_ICON_GET_PRIVATE(self)->icon_name = g_strdup (" ");

    DEBUG ("Init: application name: '%s'", XAPP_STATUS_ICON_GET_PRIVATE(self)->name);

    // Default to visible (the same behavior as GtkStatusIcon)
    XAPP_STATUS_ICON_GET_PRIVATE(self)->visible = TRUE;

    refresh_icon (self);
}


static void
xapp_status_icon_dispose (GObject *object)
{
    XAppStatusIcon *self = XAPP_STATUS_ICON (object);

    DEBUG ("XAppStatusIcon dispose (%p)", object);

    g_free (XAPP_STATUS_ICON_GET_PRIVATE(self)->name);
    g_free (XAPP_STATUS_ICON_GET_PRIVATE(self)->icon_name);
    g_free (XAPP_STATUS_ICON_GET_PRIVATE(self)->tooltip_text);
    g_free (XAPP_STATUS_ICON_GET_PRIVATE(self)->label);
    g_free (XAPP_STATUS_ICON_GET_PRIVATE(self)->metadata);

    g_clear_object (&XAPP_STATUS_ICON_GET_PRIVATE(self)->cancellable);

    g_clear_object (&XAPP_STATUS_ICON_GET_PRIVATE(self)->primary_menu);
    g_clear_object (&XAPP_STATUS_ICON_GET_PRIVATE(self)->secondary_menu);

    /* Cleanup active backend */
    if (XAPP_STATUS_ICON_GET_PRIVATE(self)->backend_ops && XAPP_STATUS_ICON_GET_PRIVATE(self)->backend_ops->cleanup)
    {
        XAPP_STATUS_ICON_GET_PRIVATE(self)->backend_ops->cleanup(self);
    }

    if (XAPP_STATUS_ICON_GET_PRIVATE(self)->listener_id > 0)
    {
        g_dbus_connection_signal_unsubscribe (XAPP_STATUS_ICON_GET_PRIVATE(self)->connection, XAPP_STATUS_ICON_GET_PRIVATE(self)->listener_id);
        XAPP_STATUS_ICON_GET_PRIVATE(self)->listener_id = 0;
    }

    g_clear_object (&XAPP_STATUS_ICON_GET_PRIVATE(self)->connection);

    G_OBJECT_CLASS (xapp_status_icon_parent_class)->dispose (object);
}

static void
xapp_status_icon_finalize (GObject *object)
{
    DEBUG ("XAppStatusIcon finalize (%p)", object);

    g_clear_object (&XAPP_STATUS_ICON_GET_PRIVATE(XAPP_STATUS_ICON (object))->cancellable);

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
    status_icon_signals[SIGNAL_BUTTON_PRESS] =
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
    status_icon_signals[SIGNAL_BUTTON_RELEASE] =
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
    status_icon_signals [SIGNAL_ACTIVATE] =
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
    status_icon_signals [SIGNAL_STATE_CHANGED] =
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
    status_icon_signals [SIGNAL_SCROLL] =
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

    if (g_strcmp0 (name, XAPP_STATUS_ICON_GET_PRIVATE(icon)->name) == 0)
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

    g_clear_pointer (&XAPP_STATUS_ICON_GET_PRIVATE(icon)->name, g_free);
    XAPP_STATUS_ICON_GET_PRIVATE(icon)->name = g_strdup (name);

    DEBUG ("set_name: %s", name);

    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->interface_skeleton)
    {
        xapp_status_icon_interface_set_name (XAPP_STATUS_ICON_GET_PRIVATE(icon)->interface_skeleton, name);
    }

    /* Call this directly instead of in the update_fallback_icon() function,
     * as every time this is called, Gtk re-creates the plug for the icon,
     * so the tray thinks one icon has disappeared and a new one appeared,
     * which can cause flicker and undesirable re-ordering of tray items. */
    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->gtk_status_icon != NULL)
    {
        gtk_status_icon_set_name (XAPP_STATUS_ICON_GET_PRIVATE(icon)->gtk_status_icon, name);
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

    if (g_strcmp0 (icon_name, XAPP_STATUS_ICON_GET_PRIVATE(icon)->icon_name) == 0)
    {
        return;
    }

    g_clear_pointer (&XAPP_STATUS_ICON_GET_PRIVATE(icon)->icon_name, g_free);
    XAPP_STATUS_ICON_GET_PRIVATE(icon)->icon_name = g_strdup (icon_name);

    DEBUG ("set_icon_name: %s", icon_name);

    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops && XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops->set_icon_name)
    {
        XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops->set_icon_name (icon, icon_name);
    }
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

    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->interface_skeleton == NULL)
    {
        DEBUG ("get_icon_size: %d (fallback)", FALLBACK_ICON_SIZE);

        return FALLBACK_ICON_SIZE;
    }

    gint size;

    size = xapp_status_icon_interface_get_icon_size (XAPP_STATUS_ICON_GET_PRIVATE(icon)->interface_skeleton);

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

    if (g_strcmp0 (tooltip_text, XAPP_STATUS_ICON_GET_PRIVATE(icon)->tooltip_text) == 0)
    {
        return;
    }

    g_clear_pointer (&XAPP_STATUS_ICON_GET_PRIVATE(icon)->tooltip_text, g_free);
    XAPP_STATUS_ICON_GET_PRIVATE(icon)->tooltip_text = g_strdup (tooltip_text);

    DEBUG ("set_tooltip_text: %s", tooltip_text);

    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops && XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops->set_tooltip)
    {
        XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops->set_tooltip (icon, tooltip_text);
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

    if (g_strcmp0 (label, XAPP_STATUS_ICON_GET_PRIVATE(icon)->label) == 0)
    {
        return;
    }

    g_clear_pointer (&XAPP_STATUS_ICON_GET_PRIVATE(icon)->label, g_free);
    XAPP_STATUS_ICON_GET_PRIVATE(icon)->label = g_strdup (label);

    DEBUG ("set_label: '%s'", label);

    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops && XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops->set_label)
    {
        XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops->set_label (icon, label);
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

    if (visible == XAPP_STATUS_ICON_GET_PRIVATE(icon)->visible)
    {
        return;
    }

    XAPP_STATUS_ICON_GET_PRIVATE(icon)->visible = visible;

    DEBUG ("set_visible: %s", visible ? "TRUE" : "FALSE");

    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops && XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops->set_visible)
    {
        XAPP_STATUS_ICON_GET_PRIVATE(icon)->backend_ops->set_visible (icon, visible);
    }
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

    DEBUG ("get_visible: %s", XAPP_STATUS_ICON_GET_PRIVATE(icon)->visible ? "TRUE" : "FALSE");

    return XAPP_STATUS_ICON_GET_PRIVATE(icon)->visible;
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
    g_return_if_fail (process_icon_state != XAPP_STATUS_ICON_STATE_NO_SUPPORT);

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

    if (menu == GTK_MENU (XAPP_STATUS_ICON_GET_PRIVATE(icon)->primary_menu))
    {
        return;
    }

    g_clear_object (&XAPP_STATUS_ICON_GET_PRIVATE(icon)->primary_menu);

    DEBUG ("%s: %p", XAPP_STATUS_ICON_GET_PRIVATE(icon)->name, menu);

    if (menu)
    {
        XAPP_STATUS_ICON_GET_PRIVATE(icon)->primary_menu = GTK_WIDGET (g_object_ref_sink (menu));
    }

    /* Update SNI backend menu if active */
    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->active_backend == XAPP_BACKEND_SNI)
    {
        sni_backend_export_menu(icon);
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

    DEBUG ("get_menu: %p", XAPP_STATUS_ICON_GET_PRIVATE(icon)->primary_menu);

    return XAPP_STATUS_ICON_GET_PRIVATE(icon)->primary_menu;
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

    if (menu == GTK_MENU (XAPP_STATUS_ICON_GET_PRIVATE(icon)->secondary_menu))
    {
        return;
    }

    g_clear_object (&XAPP_STATUS_ICON_GET_PRIVATE(icon)->secondary_menu);

    DEBUG ("%s: %p", XAPP_STATUS_ICON_GET_PRIVATE(icon)->name, menu);

    if (menu)
    {
        XAPP_STATUS_ICON_GET_PRIVATE(icon)->secondary_menu = GTK_WIDGET (g_object_ref_sink (menu));
    }

    /* Update SNI backend menu if active */
    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->active_backend == XAPP_BACKEND_SNI)
    {
        sni_backend_export_menu(icon);
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

    DEBUG ("get_menu: %p", XAPP_STATUS_ICON_GET_PRIVATE(icon)->secondary_menu);

    return XAPP_STATUS_ICON_GET_PRIVATE(icon)->secondary_menu;
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

    DEBUG ("get_state: %s", state_to_str (process_icon_state));

    return process_icon_state;
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

    if (g_strcmp0 (metadata, XAPP_STATUS_ICON_GET_PRIVATE(icon)->metadata) == 0)
    {
        return;
    }

    old_meta = XAPP_STATUS_ICON_GET_PRIVATE(icon)->metadata;
    XAPP_STATUS_ICON_GET_PRIVATE(icon)->metadata = g_strdup (metadata);
    g_free (old_meta);

    if (XAPP_STATUS_ICON_GET_PRIVATE(icon)->interface_skeleton)
    {
        xapp_status_icon_interface_set_metadata (XAPP_STATUS_ICON_GET_PRIVATE(icon)->interface_skeleton, metadata);
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

