#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "xapp-dark-mode-manager.h"

#define DEBUG_FLAG XAPP_DEBUG_DARK_MODE_MANAGER
#include "xapp-debug.h"

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_SETTINGS_INTERFACE "org.freedesktop.portal.Settings"

#define PORTAL_ERROR_NOT_FOUND "org.freedesktop.portal.Error.NotFound"

#define FALLBACK_SETTINGS "org.x.apps.portal"

/**
 * SECTION:xapp-dark-mode-manager
 * @Short_description: Manages an application's dark mode preference for it.
 * @Title: XAppDarkModeManager
 *
 * This class will attempt to use the XDG Desktop Settings portal to manage its
 * 'gtk-application-prefer-dark-theme' setting. If the portal is unavailable it
 * will resort to using xdg-desktop-portal-xapp's dconf setting.
 *
 * Since 2.6
 */

typedef enum
{
    /* Aligns with the portal spec */
    COLOR_SCHEME_DEFAULT,
    COLOR_SCHEME_PREFER_DARK,
    COLOR_SCHEME_PREFER_LIGHT,
} ColorScheme;

typedef struct
{
    GDBusProxy *portal;
    GSettings *fallback_settings;

    gboolean app_prefers_dark;

    ColorScheme color_scheme;
} XAppDarkModeManagerPrivate;

struct _XAppDarkModeManager
{
    GObject parent_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE (XAppDarkModeManager, xapp_dark_mode_manager, G_TYPE_OBJECT)

static const gchar *
color_scheme_name (ColorScheme scheme)
{
    switch (scheme)
    {
    case COLOR_SCHEME_DEFAULT:
        return "Default";
    case COLOR_SCHEME_PREFER_LIGHT:
        return "Prefer Light";
    case COLOR_SCHEME_PREFER_DARK:
        return "Prefer Dark";
    }

    return "unknown";
}

static void
update_gtk_settings (XAppDarkModeManager *manager)
{
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);
    GtkSettings *gtk_settings = gtk_settings_get_default ();

    gboolean pref = priv->app_prefers_dark;

    switch (priv->color_scheme)
    {
    case COLOR_SCHEME_PREFER_LIGHT:
        pref = FALSE;
        break;
    case COLOR_SCHEME_PREFER_DARK:
        pref = TRUE;
        break;
    case COLOR_SCHEME_DEFAULT:
    default:
        break;
    }

    g_object_set (gtk_settings, "gtk-application-prefer-dark-theme", pref, NULL);
}

static void
fallback_gsettings_changed_cb (gpointer user_data)
{
    XAppDarkModeManager *manager = XAPP_DARK_MODE_MANAGER (user_data);
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);

    priv->color_scheme = g_settings_get_enum (priv->fallback_settings, "color-scheme");

    DEBUG ("Fallback settings changed (color-scheme: %s)", color_scheme_name (priv->color_scheme));
    update_gtk_settings (manager);
}


static void
use_fallback_gsettings (XAppDarkModeManager *manager)
{
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);

    DEBUG ("Using fallback gsettings");

    priv->fallback_settings = g_settings_new (FALLBACK_SETTINGS);
    priv->color_scheme = g_settings_get_enum (priv->fallback_settings, "color-scheme");

    g_signal_connect_swapped (priv->fallback_settings,
                              "changed::color-scheme",
                              G_CALLBACK (fallback_gsettings_changed_cb),
                              manager);

    DEBUG ("Initial fallback settings read (color-scheme: %s)", color_scheme_name (priv->color_scheme));
    update_gtk_settings (manager);
}

static gboolean
read_portal_setting (XAppDarkModeManager *manager,
                     const char          *schema,
                     const char          *name,
                     const char          *type,
                     GVariant           **out)
{
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);

    GError *error = NULL;
    GVariant *ret;
    GVariant *child, *child2;
    GVariantType *out_type;
    gboolean result = FALSE;

    ret = g_dbus_proxy_call_sync (priv->portal,
                                  "Read",
                                  g_variant_new ("(ss)", schema, name),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  G_MAXINT,
                                  NULL,
                                  &error);
    if (error)
    {
        if (error->domain == G_DBUS_ERROR &&
            error->code == G_DBUS_ERROR_SERVICE_UNKNOWN)
        {
            DEBUG ("Portal not found: %s", error->message);
        }
        else
        if (error->domain == G_DBUS_ERROR &&
            error->code == G_DBUS_ERROR_UNKNOWN_METHOD)
        {
            DEBUG ("Portal doesn't provide settings: %s", error->message);
        }
        else
        if (g_dbus_error_is_remote_error (error))
        {
            char *remote_error = g_dbus_error_get_remote_error (error);

            if (!g_strcmp0 (remote_error, PORTAL_ERROR_NOT_FOUND)) {
              DEBUG ("Setting %s.%s of type %s not found", schema, name, type);
            }
            g_free (remote_error);
        }
        else
        {
            g_critical ("Couldn't read the %s setting: %s", name, error->message);
        }

        g_clear_error (&error);

        return FALSE;
    }

    g_variant_get (ret, "(v)", &child);
    g_variant_get (child, "v", &child2);

    out_type = g_variant_type_new (type);
    if (g_variant_type_equal (g_variant_get_type (child2), out_type))
    {
        *out = child2;

        result = TRUE;
    }
    else
    {
        g_critical ("Invalid type for %s.%s: expected %s, got %s",
                    schema, name, type, g_variant_get_type_string (child2));

        g_variant_unref (child2);
    }

    g_variant_type_free (out_type);
    g_variant_unref (child);
    g_variant_unref (ret);
    g_clear_error (&error);

    return result;
}

static void
portal_changed_cb (GDBusProxy  *proxy,
                   const char  *sender_name,
                   const char  *signal_name,
                   GVariant    *parameters,
                   gpointer     user_data)
{
    XAppDarkModeManager *manager = XAPP_DARK_MODE_MANAGER (user_data);
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);
    const char *namespace;
    const char *name;
    GVariant *value = NULL;

    if (g_strcmp0 (signal_name, "SettingChanged"))
        return;

    g_variant_get (parameters, "(&s&sv)", &namespace, &name, &value);

    if (!g_strcmp0 (namespace, "org.freedesktop.appearance") &&
        !g_strcmp0 (name, "color-scheme"))
    {
        priv->color_scheme = g_variant_get_uint32 (value);
        g_variant_unref (value);
    }

    DEBUG ("Portal setting changed (color-scheme: %s)", color_scheme_name (priv->color_scheme));

    update_gtk_settings (manager);
}

static void
init_and_monitor_portal (XAppDarkModeManager *manager)
{
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);
    GVariant *value = NULL;

    if (read_portal_setting (manager, "org.freedesktop.appearance",
                             "color-scheme", "u", &value))
    {
        priv->color_scheme = g_variant_get_uint32 (value);
        g_variant_unref (value);

        DEBUG ("Initial portal setting read (color-scheme: %s)", color_scheme_name (priv->color_scheme));

        update_gtk_settings (manager);

        g_signal_connect (priv->portal, "g-signal",
                          G_CALLBACK (portal_changed_cb), manager);
    }
    else
    {
        use_fallback_gsettings (manager);
    }
}

static void
new_portal_callback (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
    g_return_if_fail (XAPP_IS_DARK_MODE_MANAGER (user_data));

    XAppDarkModeManager *manager = XAPP_DARK_MODE_MANAGER (user_data);
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);

    GError *error = NULL;
    priv->portal = g_dbus_proxy_new_for_bus_finish (res, &error);

    if (error != NULL)
    {
        g_critical ("XDG desktop portal proxy failed to initialize: %s", error->message);
        g_free (error);
        use_fallback_gsettings (manager);
        return;
    }

    init_and_monitor_portal (manager);
}

static void
init_manager (XAppDarkModeManager *manager)
{
    DEBUG ("XAppDarkModeManager: init_manager");

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              PORTAL_BUS_NAME,
                              PORTAL_OBJECT_PATH,
                              PORTAL_SETTINGS_INTERFACE,
                              NULL,
                              (GAsyncReadyCallback) new_portal_callback,
                              manager);
}

static void
xapp_dark_mode_manager_init (XAppDarkModeManager *manager)
{
}

static void
xapp_dark_mode_manager_dispose (GObject *object)
{
    XAppDarkModeManager *manager = XAPP_DARK_MODE_MANAGER (object);
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);
    DEBUG ("XAppDarkModeManager dispose (%p)", object);

    g_clear_object (&priv->fallback_settings);
    g_clear_object (&priv->portal);

    G_OBJECT_CLASS (xapp_dark_mode_manager_parent_class)->dispose (object);
}

static void
xapp_dark_mode_manager_finalize (GObject *object)
{
    DEBUG ("XAppDarkModeManager finalize (%p)", object);

    G_OBJECT_CLASS (xapp_dark_mode_manager_parent_class)->finalize (object);
}

static void
xapp_dark_mode_manager_class_init (XAppDarkModeManagerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = xapp_dark_mode_manager_dispose;
    gobject_class->finalize = xapp_dark_mode_manager_finalize;
}

/**
 * xapp_dark_mode_manager_new:
 * @prefer_dark_mode: The application's preference.
 *
 *
 * Returns: (transfer full): a new XAppDarkModeManager instance. Free with g_object_unref.
 *
 * Since: 2.6
 */
XAppDarkModeManager *
xapp_dark_mode_manager_new (gboolean prefer_dark_mode)
{
    XAppDarkModeManager *manager = g_object_new (XAPP_TYPE_DARK_MODE_MANAGER, NULL);
    XAppDarkModeManagerPrivate *priv = xapp_dark_mode_manager_get_instance_private (manager);

    priv->app_prefers_dark = prefer_dark_mode;

    init_manager (manager);

    return manager;
}
