
#include "config.h"

#include <glib.h>
#include <gio/gio.h>

#include "xapp-util.h"

#define PRIME_SUPPORTED_TEST_FILE "/var/lib/ubuntu-drivers-common/requires_offloading"
#define PRIME_MODE_FILE "/etc/prime-discrete"

/**
 * xapp_util_gpu_offload_supported:
 *
 * Performs a check to see if on-demand mode for discrete graphics
 * is supported.
 *
 * Returns: %TRUE if supported.
 *
 * Since: 1.8
 */
gboolean
xapp_util_gpu_offload_supported (void)
{
    g_autoptr(GFile) modefile = NULL;
    g_autofree gchar *contents = NULL;

    if (!g_file_test (PRIME_SUPPORTED_TEST_FILE, G_FILE_TEST_EXISTS))
    {
        return FALSE;
    }

    modefile = g_file_new_for_path (PRIME_MODE_FILE);

    if (!g_file_load_contents (modefile,
                               NULL,
                               &contents,
                               NULL,
                               NULL,
                               NULL))
    {
        return FALSE;
    }

    return g_strstr_len (contents, -1, "on-demand") != NULL;
}

// Copied from cinnamon:main.c
/**
 * xapp_util_get_session_is_running:
 *
 * Check if the Session Manager is currently in the "Running" phase.
 *
 * Returns: %TRUE if the session is running.
 *
 * Since: 2.0
 */
gboolean
xapp_util_get_session_is_running (void)
{
    GDBusConnection *session_bus;
    GError *error;
    GVariant *session_result;
    gboolean session_running;

    error = NULL;

    session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION,
                                  NULL,
                                  &error);

    if (error != NULL)
    {
        g_critical ("Unable to determine if session is running, could not get session bus: %s\n", error->message);
        g_clear_error (&error);

        return FALSE;
    }

    session_result = g_dbus_connection_call_sync (session_bus,
                                                  "org.gnome.SessionManager",
                                                  "/org/gnome/SessionManager",
                                                  "org.gnome.SessionManager",
                                                  "IsSessionRunning",
                                                  NULL,
                                                  G_VARIANT_TYPE ("(b)"),
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  1000,
                                                  NULL,
                                                  &error);

    if (session_result != NULL)
    {
        g_variant_get (session_result, "(b)", &session_running);
        g_variant_unref (session_result);
    }
    else
    {
        session_running = FALSE;
        g_clear_error (&error);
    }

    g_object_unref (session_bus);

    return session_running;
}
