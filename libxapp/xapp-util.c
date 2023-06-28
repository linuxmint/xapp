
#include "config.h"

#include <glib.h>
#include <gio/gio.h>
#include <pango/pango.h>

#include "xapp-util.h"

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
    const gchar *SWITCHEROO_PATH   = "/net/hadess/SwitcherooControl";
    const gchar *SWITCHEROO_OBJECT = "net.hadess.SwitcherooControl";

    GDBusConnection *switcheroo_connection;
    GDBusProxy *switcheroo_proxy;
    GError *error;
    GVariant* switcheroo_property;
    gboolean has_dual_gpu;

    error = NULL;

    switcheroo_connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
                            NULL,
                            &error);

    if (error != NULL) {
        g_critical ("Unable to determine if GPU offload is supported, could not get system bus: %s\n", error->message);
	g_clear_error (&error);
	return FALSE;
    }

    switcheroo_proxy = g_dbus_proxy_new_sync (switcheroo_connection,
					      G_DBUS_PROXY_FLAGS_NONE,
					      NULL,
					      SWITCHEROO_OBJECT,
					      SWITCHEROO_PATH,
					      SWITCHEROO_OBJECT,
					      NULL,
					      &error);

    if (error != NULL) {
	g_debug ("Unable to determine if GPU offload is supported, could not create switcheroo-control dbus proxy: %s\n", error->message);
	g_clear_error (&error);
	return FALSE;
    }

    switcheroo_property = g_dbus_proxy_get_cached_property (switcheroo_proxy, "HasDualGpu");
    has_dual_gpu = g_variant_get_boolean (switcheroo_property);

    g_object_unref (switcheroo_connection);
    g_object_unref (switcheroo_proxy);
    g_object_unref (switcheroo_property);
    return has_dual_gpu;
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

/**
 * xapp_pango_font_string_to_css:
 * @pango_font_string: a pango font description string
 *
 * Converts a pango font description string to a string suitable for use with the css "font" tag. The font description must contain the font family and font size or conversion will fail and %NULL will be returned
 *
 * Returns: (transfer full): the css compatible font string or %NULL if the conversion failed.
 *
 * Since: 2.2
 */
gchar *
xapp_pango_font_string_to_css (const char *pango_font_string)
{
    PangoFontDescription *desc;
    GString *font_string;
    PangoFontMask set;

    desc = pango_font_description_from_string (pango_font_string);
    font_string = g_string_new ("");

    set = pango_font_description_get_set_fields (desc);

    if (!(set & PANGO_FONT_MASK_SIZE) || !(set & PANGO_FONT_MASK_FAMILY))
    {
        return NULL;
    }

    if (set & PANGO_FONT_MASK_STYLE)
    {
        switch (pango_font_description_get_style (desc))
        {
            case PANGO_STYLE_NORMAL:
                g_string_append (font_string, "normal ");
                break;
            case PANGO_STYLE_OBLIQUE:
                g_string_append (font_string, "oblique ");
                break;
            case PANGO_STYLE_ITALIC:
                g_string_append (font_string, "italic ");
                break;
            default:
                break;
        }
    }

    if (set & PANGO_FONT_MASK_VARIANT)
    {
        switch (pango_font_description_get_variant (desc))
        {
            case PANGO_VARIANT_NORMAL:
                g_string_append (font_string, "normal ");
                break;
            case PANGO_VARIANT_SMALL_CAPS:
                g_string_append (font_string, "small-caps ");
                break;
            default:
                break;
        }
    }

    if (set & PANGO_FONT_MASK_WEIGHT)
    {
        switch (pango_font_description_get_weight (desc))
        {
            case PANGO_WEIGHT_THIN:
                g_string_append (font_string, "100 ");
                break;
            case PANGO_WEIGHT_ULTRALIGHT:
                g_string_append (font_string, "200 ");
                break;
            case PANGO_WEIGHT_LIGHT:
            case PANGO_WEIGHT_SEMILIGHT:
                g_string_append (font_string, "300 ");
                break;
            case PANGO_WEIGHT_BOOK:
            case PANGO_WEIGHT_NORMAL:
                g_string_append (font_string, "400 ");
                break;
            case PANGO_WEIGHT_MEDIUM:
                g_string_append (font_string, "500 ");
                break;
            case PANGO_WEIGHT_SEMIBOLD:
                g_string_append (font_string, "600 ");
                break;
            case PANGO_WEIGHT_BOLD:
                g_string_append (font_string, "700 ");
                break;
            case PANGO_WEIGHT_ULTRABOLD:
                g_string_append (font_string, "800 ");
                break;
            case PANGO_WEIGHT_HEAVY:
            case PANGO_WEIGHT_ULTRAHEAVY:
                g_string_append (font_string, "900 ");
                break;
            default:
                break;
        }
    }

    if (set & PANGO_FONT_MASK_STRETCH)
    {
        switch (pango_font_description_get_stretch (desc))
        {
            case PANGO_STRETCH_ULTRA_CONDENSED:
                g_string_append (font_string, "ultra-condensed ");
                break;
            case PANGO_STRETCH_EXTRA_CONDENSED:
                g_string_append (font_string, "extra-condensed ");
                break;
            case PANGO_STRETCH_CONDENSED:
                g_string_append (font_string, "condensed ");
                break;
            case PANGO_STRETCH_SEMI_CONDENSED:
                g_string_append (font_string, "semi-condensed ");
                break;
            case PANGO_STRETCH_NORMAL:
                g_string_append (font_string, "normal ");
                break;
            case PANGO_STRETCH_SEMI_EXPANDED:
                g_string_append (font_string, "semi-expanded ");
                break;
            case PANGO_STRETCH_EXPANDED:
                g_string_append (font_string, "expanded ");
                break;
            case PANGO_STRETCH_EXTRA_EXPANDED:
                break;
            case PANGO_STRETCH_ULTRA_EXPANDED:
                g_string_append (font_string, "ultra-expanded ");
                break;
            default:
                break;
        }
    }

    g_string_append_printf (font_string, "%dpx ", pango_font_description_get_size (desc) / PANGO_SCALE);
    g_string_append (font_string, pango_font_description_get_family (desc));

    return g_string_free (font_string, FALSE);
}


/**
 * xapp_get_tmp_dir:
 *
 * Provides the path to the system's temporary files folder. This is identical to g_get_tmp_dir,
 * but includes the /dev/shm ramdisk as the first choice for a temporary folder.
 * 
 * Returns: (type filename) (transfer none): the directory to use for temporary files.
 * Since: 2.2.16
 */
const gchar *
xapp_get_tmp_dir (void)
{
    static const gchar *tmp_dir = NULL;

    if (tmp_dir == NULL)
    {
        if (access ("/dev/shm", W_OK) == 0)
        {
            tmp_dir = "/dev/shm";
        }
        else
        {
            tmp_dir = g_get_tmp_dir ();
        }
    }

    return tmp_dir;
}
